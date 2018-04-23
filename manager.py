#! /usr/bin/python3

import networkx as nx
import twisted
from twisted.web.static import File
from klein import Klein
import json
from twisted.web.server import Site, Request
from twisted.internet.protocol import DatagramProtocol
from twisted.internet import reactor, defer, threads, endpoints, address
from twisted.internet.task import LoopingCall
import docker
import docker.errors
import time
import datetime
import copy
import base64
import socket
import itertools

import ecdsa
#import rsa

from OpenSSL import crypto

from cryptography.hazmat.primitives.hashes import Hash, SHA256
from cryptography.hazmat.primitives.asymmetric import rsa, ec, padding, utils
from cryptography.hazmat.primitives.serialization import load_pem_public_key, load_der_public_key
from cryptography.hazmat.backends import default_backend

# key management --------------------------------------------------------------------------------------------------------
keys = {}


def verifySignature(message: bytes, signature: bytes, key_name: str) -> bool:
    if key_name in keys:
        try:
            cert = crypto.X509()
            cert.set_pubkey(keys[key_name])
            crypto.verify(cert, signature, message, "sha256")
        except crypto.Error:
            print("[", str(datetime.datetime.now()), "] [ verifySignature ] signature is not valid with key", key_name)
            return False
        print("[", str(datetime.datetime.now()), "] [ verifySignature ] signature is valid with key", key_name)
        return True
    else:
        print("[", str(datetime.datetime.now()), "] [ verifySignature ] no key with name", key_name)
        return False


@defer.inlineCallbacks
def addKeyToSVs(key_name):
    print("[", str(datetime.datetime.now()), "] [ addKeyToSVs ] start")
    for name in [name for name, attrs in graph.nodes(data=True) if attrs["type"] == "SV"]:
        key_tuples = [(key_name, "RSA", keys[key_name])]
        resp = yield modules_socket.addKeys(name, key_tuples)
        if resp and resp == "success":
            print("[", str(datetime.datetime.now()), "] [ addKeyToSVs ] successfully add key", key_name, "to", name)
    print("[", str(datetime.datetime.now()), "] [ addKeyToSVs ] end")


@defer.inlineCallbacks
def addKeysToSV(node_name):
    print("[", str(datetime.datetime.now()), "] [ addKeysToSV ] start")
    if graph.nodes[node_name]["type"] == "SV":
        key_tuples = []
        for key in keys:
            key_tuples.append((key, "RSA", keys[key]))
        # send keys 10 by 10 just to be sure to not exceed UDP packet size (way too restrictive)
        for chunk in [key_tuples[x:x + 10] for x in range(0, len(key_tuples), 10)]:
            resp = yield modules_socket.addKeys(node_name, chunk)
            if resp and resp == "success":
                pass
    print("[", str(datetime.datetime.now()), "] [ addKeysToSV ] end")


@defer.inlineCallbacks
def delSVsKey(key_name):
    print("[", str(datetime.datetime.now()), "] [ delSVsKey ] start")
    for name in [name for name, attrs in graph.nodes(data=True) if attrs["type"] == "SV"]:
        resp = yield modules_socket.delKeys(name, [key_name])
        if resp and resp == "success":
            pass
    print("[", str(datetime.datetime.now()), "] [ delSVsKey ] end")


SVs_to_check = {}
@defer.inlineCallbacks
def IncreaseSVExpirationCounter(name):
    graph.nodes[name]["packet_stats"]["expiration_counter"] += 1
    print("[", str(datetime.datetime.now()), "] [ delSVsKey ]", name, "-> expiration set to", graph.nodes[name]["packet_stats"]["expiration_counter"])
    if graph.nodes[name]["packet_stats"]["expiration_counter"] >= 10:
        result = SVs_to_check.pop(name, None)
        if result:
            result.stop()
            yield detachNode(name)
            yield removeContainer(name)
            graph.remove_node(name)


# graph ----------------------------------------------------------------------------------------------------------------
graph = nx.DiGraph()
graph_counters = {"CS": 1, "BR": 1, "NR": 1, "SR": 1, "SV": 1, "PD": 1, "LA": 1, "FW": 1}
node_default_attrs = {
    "cpu_stats": {
        "cpu_percent": 0.0,
        "cpu_system": 0,
        "cpu_total": 0,
        "last_update": 0.0
    }
}
specific_node_default_attrs = {
    "CS": {
        "type": "CS",
        "size": 100000,
        "cache_stats": {
            "cache_hit": 0.0,
            "hit_count": 0,
            "miss_count": 0,
            "last_update": 0.0
        },
        "cpu_quota": 100000
    },
    "BR": {
        "type": "BR",
        "size": 250,
        "cpu_quota": 50000
    },
    "NR": {
        "type": "NR",
        "static_routes": {},
        "dynamic_routes": {},
        "cpu_quota": 100000
    },
    "SR": {
        "type": "SR",
        "strategy": "multicast",
        "cpu_quota": 400000
    },
    "SV": {
        "type": "SV",
        "packet_stats": {
            "fake_count": 0,
            "last_update": 0.0,
            "expiration_counter": 0
        },
        "cpu_quota": 100000
    }
}
locked_nodes = set()

@defer.inlineCallbacks
def propagateNewRoutes(name: str, prefixes: list):
    print("[", str(datetime.datetime.now()), "] [ propagateNewRoutes ] start")
    NR_names = [name2 for name2, attrs in graph.nodes(data=True) if attrs["type"] == "NR" and name2 != name]
    for name2 in NR_names:
        for path in nx.all_simple_paths(graph, name2, name):
            print("[", str(datetime.datetime.now()), "] [ propagateNewRoutes ]", name2, "-> add dynamic routes", prefixes, "to", path[1])
            face_id = graph.edges[name2, path[1]]["face_id"]
            resp = yield modules_socket.addRoutes(name2, face_id, prefixes)
            if resp and resp == "success":
                routes = graph.nodes[name2]["dynamic_routes"].get(face_id, set())
                routes.update(prefixes)
                graph.nodes[name2]["dynamic_routes"][face_id] = routes
            else:
                print("[", str(datetime.datetime.now()), "] [ propagateNewRoutes ]", name2, "-> error while adding routes")
    print("[", str(datetime.datetime.now()), "] [ propagateNewRoutes ] end")

@defer.inlineCallbacks
def propagateDelRoutes(name: str, prefixes: list):
    print("[", str(datetime.datetime.now()), "] [ propagateDelRoutes ] start")
    nodes_with_dynamic_routes = [(name, attr) for name, attr in graph.nodes(data="dynamic_routes") if attr]
    nodes_with_static_routes = [(name, attr) for name, attr in graph.nodes(data="static_routes") if attr]
    for name2, attr in nodes_with_dynamic_routes:
        for path in nx.all_simple_paths(graph, name2, name):
            accessible_static_prefixes = set()
            for name3, attr in nodes_with_static_routes:
                if nx.has_path(graph, path[1], name3):
                    accessible_static_prefixes.update(itertools.chain.from_iterable(attr.values()))
            prefixes_to_remove = list(set(prefixes) - accessible_static_prefixes)
            if prefixes_to_remove:
                print("[", str(datetime.datetime.now()), "] [ propagateDelRoutes ]", name2, "-> remove dynamic routes", prefixes_to_remove, "to", path[1])
                face_id2 = graph.edges[name2, path[1]]["face_id"]
                resp = yield modules_socket.delRoutes(name2, face_id2, prefixes_to_remove)
                if resp and resp == "success":
                    graph.nodes[name2]["dynamic_routes"].pop(face_id2, None)
                else:
                    print("[", str(datetime.datetime.now()), "] [ propagateDelRoutes ]", name2, "-> error while removing routes")
            else:
                print("[", str(datetime.datetime.now()), "] [ propagateDelRoutes ]", name2, "-> routes are up to date")
    print("[", str(datetime.datetime.now()), "] [ propagateDelRoutes ] end")

# when a new node connect to the network
@defer.inlineCallbacks
def appendExistingRoutes(source: str, target: str):
    print("[", str(datetime.datetime.now()), "] [ appendExistingRoutes ] start")
    nodes_with_static_routes = [(name, attr) for name, attr in graph.nodes(data="static_routes") if attr]
    accessible_static_prefixes = set()
    for name, attr in nodes_with_static_routes:
        if nx.has_path(graph, target, name):
            accessible_static_prefixes.update(itertools.chain.from_iterable(attr.values()))
    if accessible_static_prefixes:
        if graph.nodes[source]["type"] == "NR":
            print("[", str(datetime.datetime.now()), "] [ appendExistingRoutes ]", source, "-> add dynamic routes", accessible_static_prefixes, "to", target)
            face_id = graph.edges[source, target]["face_id"]
            resp = yield modules_socket.addRoutes(source, face_id, accessible_static_prefixes)
            if resp and resp == "success":
                graph.nodes[source]["dynamic_routes"][face_id] = accessible_static_prefixes
        else:
            print("[", str(datetime.datetime.now()), "] [ appendExistingRoutes ]", source, "-> skip, not a NR node")
        propagateNewRoutes(source, list(accessible_static_prefixes))
    print("[", str(datetime.datetime.now()), "] [ appendExistingRoutes ] end")


@defer.inlineCallbacks
def updateContainersCpuStats():
    print("[", str(datetime.datetime.now()), "] [ updateContainersCpuStats ] start")
    nodes = list(graph.nodes(data=True))
    resps = yield defer.DeferredList([threads.deferToThread(getContainerStats, node[0]) for node in nodes], consumeErrors=True)
    with open("logs.csv", "a") as f:
        for node, resp in zip(nodes, resps):
            cpu_stats = node[1]["cpu_stats"]
            if resp[0]:
                if resp[1]["cpu_stats"].get("system_cpu_usage", None):
                    p, s, t = calculate_cpu_percent2(resp[1], cpu_stats["cpu_total"], cpu_stats["cpu_system"])
                    cpu_stats["cpu_percent"] = round(p, 1)
                    f.write(node[0] + "; " + str(cpu_stats["cpu_percent"]) + "; ")
                    cpu_stats["cpu_system"] = s
                    cpu_stats["cpu_total"] = t
                    cpu_stats["last_update"] = time.time()
                    graph.nodes[node[0]]["cpu_stats"] = cpu_stats
                    print("[", str(datetime.datetime.now()), "] [ updateContainersCpuStats ]", node[0], "-> CPU:", cpu_stats["cpu_percent"])
                else:
                    print("[", str(datetime.datetime.now()), "] [ updateContainersCpuStats ]", node[0], "is probably not in 'running' state")
            else:
                if resp[1].type == docker.errors.NotFound:
                    print("[", str(datetime.datetime.now()), "] [ updateContainersCpuStats ]", node[0], "doesn't exist")
                elif resp[1].type == docker.errors.APIError:
                    print("[", str(datetime.datetime.now()), "] [ updateContainersCpuStats ] docker api error")
                else:
                    print("[", str(datetime.datetime.now()), "] [ updateContainersCpuStats ] unknown error with", node[0], "->", resp[1].type)
        f.write("\n")
    print("[", str(datetime.datetime.now()), "] [ updateContainersCpuStats ] end")


@defer.inlineCallbacks
def autoScale():
    print("[", str(datetime.datetime.now()), "] [ autoScale ] start")
    scale_up_functions = {"BR": scaleUpBR, "NR": scaleUpNR}
    scale_down_functions = {"NR": scaleDownNR}
    for name, attrs in list(graph.nodes(data=True)):
        if attrs["scalable"] and name not in locked_nodes:
            locked_nodes.add(name)
            if attrs["cpu_stats"]["cpu_percent"] >= attrs["cpu_quota"] * 0.0009 and list(graph.predecessors(name)) and list(graph.successors(name)):
                print("[", str(datetime.datetime.now()), "] [ autoScale ] scale up", name)
                yield scale_up_functions.get(attrs["type"], scaleUp)(name, attrs)
            elif attrs.get("scaled", False) and attrs["cpu_stats"]["cpu_percent"] <= attrs["cpu_quota"] * 0.0002:
                print("[", str(datetime.datetime.now()), "] [ autoScale ] scale down", name)
                yield scale_down_functions.get(attrs["type"], scaleDown)(name, attrs)
            else:
                print("[", str(datetime.datetime.now()), "] [ autoScale ]", name, "-> nothing to do")
            locked_nodes.remove(name)
    print("[", str(datetime.datetime.now()), "] [ autoScale ] end")


@defer.inlineCallbacks
def scaleUp(name, attrs):
    print("[", str(datetime.datetime.now()), "] [ scaleUp ] start")
    scale = attrs.get("scale", 1)
    in_node_names = list(graph.predecessors(name))
    out_node_names = list(graph.successors(name))
    lb_name = name + ".SR1"
    # if n == 1 -> no SRs so create one, else in_node_list is SRs
    print("[", str(datetime.datetime.now()), "] [ scaleUp ]", "step 1")
    if scale == 1:
        # create SR
        if createContainer(lb_name, "SR"):
            graph.add_node(lb_name, editable=False, scalable=False, addresses=getContainerIPAddresses(lb_name),
                           **copy.deepcopy(node_default_attrs), **copy.deepcopy(specific_node_default_attrs["SR"]))
            print("[", str(datetime.datetime.now()), "] [ scaleUp ]", lb_name, "created")
            # set strategy to loadbalancing
            resp = yield modules_socket.editConfig(lb_name, {"strategy": "loadbalancing"})
            if resp and "strategy" in resp:
                print("[", str(datetime.datetime.now()), "] [ scaleUp ]", "set", lb_name, "strategy to loadbalancing")
                attachNode(lb_name, in_node_names, [name])
    print("[", str(datetime.datetime.now()), "] [ scaleUp ]", "step 2")
    clone_name = name + "." + str(scale)
    # create a clone
    if createContainer(clone_name, attrs["type"]):
        print("[", str(datetime.datetime.now()), "] [ scaleUp ]", clone_name, "created")
        graph.add_node(clone_name, editable=False, scalable=False, addresses=getContainerIPAddresses(clone_name),
                       **copy.deepcopy(node_default_attrs), **copy.deepcopy(specific_node_default_attrs[attrs["type"]]))
        #print("[", str(datetime.datetime.now()), "] [ scaleUpBR ]", attrs["size"], node_default_attrs[attrs["type"]]["size"])
        #if attrs["size"] != node_default_attrs[attrs["type"]]["size"]:
        #    resp = yield modules_socket.editConfig(clone_name, {"size": attrs["size"]})
        #    if resp:
        #        print("[", str(datetime.datetime.now()), "] [ scaleUpBR ]", resp)
        # for each successor
        attachNode(clone_name, [lb_name], out_node_names, True)
    attrs["scale"] = scale + 1
    attrs["scaled"] = True
    print("[", str(datetime.datetime.now()), "] [ scaleUp ] end")


@defer.inlineCallbacks
def scaleUpBR(name, attrs):
    print("[", str(datetime.datetime.now()), "] [ scaleUpBR ] start")
    scale = attrs.get("scale", 1)
    in_node_list = list(graph.predecessors(name))
    out_node_list = list(graph.successors(name))
    lb_nodes_list = []
    # if n == 1 -> no SRs so create one, else in_node_list is SRs
    print("[", str(datetime.datetime.now()), "] [ scaleUpBR ]", "step 1")
    if scale == 1:
        for i in range(len(in_node_list)):
            lb_name = name + ".SR" + str(i+1)
            # create and link SR to scaled node
            if createContainer(lb_name, "SR"):
                graph.add_node(lb_name, editable=False, scalable=False, addresses=getContainerIPAddresses(lb_name),
                               **copy.deepcopy(node_default_attrs), **copy.deepcopy(specific_node_default_attrs["SR"]))
                print("[", str(datetime.datetime.now()), "] [ scaleUpBR ]", lb_name, "created")
                resp = yield modules_socket.editConfig(lb_name, {"strategy": "loadbalancing"})
                if resp and "strategy" in resp:
                    print("[", str(datetime.datetime.now()), "] [ scaleUpBR ]", "set", lb_name, "strategy to loadbalancing")
                resp = yield modules_socket.addFace(lb_name, name)
                if resp and resp > 0:
                    print("[", str(datetime.datetime.now()), "] [ scaleUpBR ]", lb_name, "linked to", name)
                    graph.add_edge(lb_name, name, face_id=resp)
                    lb_nodes_list.append(lb_name)
                    resp = yield modules_socket.addFace(in_node_list[i], lb_name)
                    if resp and resp > 0:
                        print("[", str(datetime.datetime.now()), "] [ scaleUpBR ]", in_node_list[i], "linked to", lb_name)
                        graph.add_edge(in_node_list[i], lb_name, face_id=resp)
                        resp = yield modules_socket.delFace(in_node_list[i], name)
                        if resp and resp > 0:
                            print("[", str(datetime.datetime.now()), "] [ scaleUpBR ]", in_node_list[i], "no longer linked to", name)
                            graph.remove_edge(in_node_list[i], name)
    else:
        lb_nodes_list = in_node_list
    print("[", str(datetime.datetime.now()), "] [ scaleUpBR ]", "step 2")
    clone_name = name + "." + str(scale)
    if createContainer(clone_name, attrs["type"]):
        print("[", str(datetime.datetime.now()), "] [ scaleUpBR ]", clone_name, "created")
        graph.add_node(clone_name, editable=False, scalable=False, addresses=getContainerIPAddresses(clone_name),
                       **copy.deepcopy(node_default_attrs), **copy.deepcopy(specific_node_default_attrs[attrs["type"]]))
        #print("[", str(datetime.datetime.now()), "] [ scaleUpBR ]", attrs["size"], node_default_attrs[attrs["type"]]["size"])
        #if attrs["size"] != node_default_attrs[attrs["type"]]["size"]:
        #    resp = yield modules_socket.editConfig(clone_name, {"size": attrs["size"]})
        #    if resp:
        #        print("[", str(datetime.datetime.now()), "] [ scaleUpBR ]", resp)
        for out_name in out_node_list:
            resp = yield modules_socket.addFace(clone_name, out_name)
            if resp and resp > 0:
                print("[", str(datetime.datetime.now()), "] [ scaleUpBR ]", clone_name, "linked to", out_name)
                graph.add_edge(clone_name, out_name, face_id=resp)
        for lb_name in lb_nodes_list:
            resp = yield modules_socket.addFace(lb_name, clone_name)
            if resp and resp > 0:
                print("[", str(datetime.datetime.now()), "] [ scaleUpBR ]", lb_name, "linked to", clone_name)
                graph.add_edge(lb_name, clone_name, face_id=resp)
    attrs["scale"] = scale + 1
    attrs["scaled"] = True
    print("[", str(datetime.datetime.now()), "] [ scaleUpBR ] end")


@defer.inlineCallbacks
def scaleDown(name, attrs):
    print("[", str(datetime.datetime.now()), "] [ scaleDown ] start")
    scale = attrs["scale"] - 1
    lb_node_list = list(graph.predecessors(name))
    out_node_list = list(graph.successors(name))
    print("[", str(datetime.datetime.now()), "] [ scaleDown ] step 1")
    clone_name = name + "." + str(scale)
    # for each SR
    for lb_name in lb_node_list:
        # unlink SR and last clone
        resp = yield modules_socket.delFace(lb_name, clone_name)
        if resp:
            print("[", str(datetime.datetime.now()), "] [ scaleDown ]", lb_name, "no longer linked to", clone_name)
            graph.remove_edge(lb_name, clone_name)
    # for each successor
    for out_name in out_node_list:
        # unlink last clone and successor
        resp = yield modules_socket.delFace(clone_name, out_name)
        if resp:
            print("[", str(datetime.datetime.now()), "] [ scaleDown ]", clone_name, "no longer linked to", out_name)
            graph.remove_edge(clone_name, out_name)
    # remove last clone
    resp = yield threads.deferToThread(removeContainer, clone_name)
    if resp:
        print("[", str(datetime.datetime.now()), "] [ scaleDown ]", clone_name, "removed")
        graph.remove_node(clone_name)
    attrs["scale"] = scale
    print("[", str(datetime.datetime.now()), "] [ scaleDown ] step 2")
    # if no clone
    if scale == 1:
        # for each SR
        for lb_name in lb_node_list:
            # for each predecessor
            for in_name in list(graph.predecessors(lb_name)):
                # link SR predecessor to node
                resp = yield modules_socket.addFace(in_name, name)
                if resp and resp > 0:
                    print("[", str(datetime.datetime.now()), "] [ scaleDown ]", in_name, "linked to", name)
                    graph.add_edge(in_name, name, face_id=resp)
                    # unlink SR predecessor and SR
                    resp = yield modules_socket.delFace(in_name, lb_name)
                    if resp:
                        print("[", str(datetime.datetime.now()), "] [ scaleDown ]", in_name, "no longer linked to", lb_name)
                        graph.remove_edge(in_name, lb_name)
                        # unlink SR and node
                        resp = yield modules_socket.delFace(lb_name, name)
                        if resp:
                            print("[", str(datetime.datetime.now()), "] [ scaleDown ]", lb_name, "no longer linked to", name)
                            graph.remove_edge(lb_name, name)
            # remove SR
            resp = yield threads.deferToThread(removeContainer, lb_name)
            if resp:
                print("[", str(datetime.datetime.now()), "] [ scaleDown ]", lb_name, "removed")
                graph.remove_node(lb_name)
        attrs["scaled"] = False
    print("[", str(datetime.datetime.now()), "] [ scaleDown ] end")


def scaleDownNR(name, attrs):
    print("[", str(datetime.datetime.now()), "] [ scaleDownNR ] pass")
    pass

@defer.inlineCallbacks
def attachNode(name, in_node_names: list, out_node_names: list, new_link=False):
    print("[", str(datetime.datetime.now()), "] [ attchNode ] start")
    #       OUT
    #      / |
    #  1(add)|
    #    /   |
    #  NODE  3(del)
    #    \   |
    #  2(add)|
    #      \ |
    #       IN
    # step 1
    for out_node_name in out_node_names:
        resp = yield modules_socket.addFace(name, out_node_name)
        if resp and resp > 0:
            print("[", str(datetime.datetime.now()), "] [ attchNode ]", name, "linked to", out_node_name)
            graph.add_edge(name, out_node_name, face_id=resp)
    # step 2
    for in_node_name in in_node_names:
        resp = yield modules_socket.addFace(in_node_name, name)
        if resp and resp > 0:
            print("[", str(datetime.datetime.now()), "] [ attchNode ]", in_node_name, "linked to", name)
            graph.add_edge(in_node_name, name, face_id=resp)
            # step 3
            if not new_link:
                for out_node_name in out_node_names:
                    if graph.has_edge(in_node_name, out_node_name):
                        resp = yield modules_socket.delFace(in_node_name, out_node_name)
                        if resp and resp > 0:
                            print("[", str(datetime.datetime.now()), "] [ attchNode ]", in_node_name, "no longer linked to", out_node_name)
                            graph.remove_edge(in_node_name, out_node_name)
    print("[", str(datetime.datetime.now()), "] [ attchNode ] end")

@defer.inlineCallbacks
def detachNode(name):
    print("[", str(datetime.datetime.now()), "] [ detachNode ] start")
    #       OUT
    #      / |
    #  3(del)|
    #    /   |
    #  NODE  1(add)
    #    \   |
    #  2(del)|
    #      \ |
    #       IN
    in_node_names = list(graph.predecessors(name))
    out_node_names = list(graph.successors(name))
    for out_node_name in out_node_names:
        for in_name in in_node_names:
            resp = yield modules_socket.addFace(in_name, out_node_name)
            if resp and resp > 0:
                print("[", str(datetime.datetime.now()), "] [ detachNode ] link", in_name, "to", out_node_name)
                graph.add_edge(in_name, out_node_name, face_id=resp)
                resp = yield modules_socket.delFace(in_name, name)
                if resp:
                    print("[", str(datetime.datetime.now()), "] [ detachNode ] unlink", in_name, "and", name)
                    graph.remove_edge(in_name, name)
        resp = yield modules_socket.delFace(name, out_node_name)
        if resp:
            print("[", str(datetime.datetime.now()), "] [ detachNode ] unlink", name, "and", out_node_name)
            graph.remove_edge(name, out_node_name)
    print("[", str(datetime.datetime.now()), "] [ detachNode ] end")


# API ------------------------------------------------------------------------------------------------------------------
app = Klein()

def jsonSerial(obj):
    """JSON serializer for objects not serializable by default json code"""

    if isinstance(obj, (set, tuple)):
        return list(obj)
    if isinstance(obj, crypto.PKey):
        return crypto.dump_publickey(crypto.FILETYPE_PEM, obj).decode()
    raise TypeError("Type %s not serializable" % type(obj))


# API static files -----------------------------------------------------------------------------------------------------
@app.route("/", branch=True)
def indexFile(request):
    return File("./http")


@app.route("/<name>", branch=True)
def staticFile(request: Request, name):
    return File("./http/" + name)


# API graph ------------------------------------------------------------------------------------------------------------
@app.route("/api/graph")
def sendGraph(request: Request):
    request.setHeader(b"Content-Type", b"application/json")
    return json.dumps(nx.node_link_data(graph), default=jsonSerial)


# API nodes ------------------------------------------------------------------------------------------------------------
@app.route("/api/nodes", methods=["POST"])
@defer.inlineCallbacks
def createNode(request: Request):
    j = json.loads(request.content.read().decode())
    if all(field in j for field in ["type"]) and j["type"] in graph_counters:
        name = j["type"] + str(graph_counters[j["type"]])
        graph_counters[j["type"]] += 1
        if createContainer(name, j["type"]):
            graph.add_node(name, editable=True, scalable=True, addresses=getContainerIPAddresses(name),
                           **copy.deepcopy(node_default_attrs), **copy.deepcopy(specific_node_default_attrs[j["type"]]))
            if j["type"] == "NR":
                resp = yield modules_socket.editConfig(name, {"manager_address": "172.19.0.1", "manager_port": 9999})
            if j["type"] == "SV":
                addKeysToSV(name)
                return name + " test"
            return name
        else:
            request.setResponseCode(500)
            return
    else:
        request.setResponseCode(400)
        return


@app.route("/api/nodes", methods=["GET"])
def sendNodesList(request: Request):
    request.setHeader(b"Content-Type", b"application/json")
    return json.dumps(list(graph.nodes), default=jsonSerial)


@app.route("/api/nodes/<name>", methods=["GET"])
def sendNodeInfo(request: Request, name):
    if graph.has_node(name):
        request.setHeader(b"Content-Type", b"application/json")
        return json.dumps(graph.nodes[name], default=jsonSerial)
    else:
        request.setResponseCode(404)
        return


@app.route("/api/nodes/<name>", methods=["DELETE"])
@defer.inlineCallbacks
def removeNode(request: Request, name):
    if graph.has_node(name):
        try:
            attrs = graph.nodes[name]
            if attrs["editable"] and name not in locked_nodes:
                locked_nodes.add(name)
                print("[", str(datetime.datetime.now()), "] [ removeNode ] unscale", name)
                while attrs.get("scaled", False):
                    yield scaleDown(name, attrs)
                detachNode(name)
                resp = yield threads.deferToThread(removeContainer, name)
                if resp:
                    print("[", str(datetime.datetime.now()), "] [ removeNode ]", name, "removed")
                    graph.remove_node(name)
                locked_nodes.remove(name)
            else:
                request.setResponseCode(503)
                return name + " is not editable or locked"
        except Exception as e:
            print(e)
        return "1"
    else:
        request.setResponseCode(404)
        return "0"

@app.route("/api/nodes/<name>", methods=["PATCH"])
@defer.inlineCallbacks
def nodeReport(request: Request, name):
    j = json.loads(request.content.read().decode())
    if graph.has_node(name):
        d = {}
        if j.get("manager_address", None):
            d["manager_address"] = j["manager_address"]
        if j.get("manager_port", None):
            d["manager_port"] = int(j["manager_port"])
        if j.get("report_each", None):
            d["report_each"] = int(j["report_each"])
        if j.get("strategy", None):
            d["strategy"] = j["strategy"]
        resp = yield modules_socket.editConfig(name, d)
        if resp:
            request.setHeader(b"Content-Type", b"application/json")
            return json.dumps(resp, default=jsonSerial)
        else:
            request.setResponseCode(204)
            return
    else:
        request.setResponseCode(404)
        return

# API links ------------------------------------------------------------------------------------------------------------
@app.route("/api/links", methods=["POST"])
@defer.inlineCallbacks
def createLink(request: Request):
    j = json.loads(request.content.read().decode())
    if (all(field in j for field in ["source", "target"]) and graph.has_node(j["source"]) and graph.has_node(j["target"])
            and not graph.has_edge(j["source"], j["target"])):
        if graph.nodes[j["target"]]["type"] == "NR":
            as_producer = j.get("as_producer", False) is True
            resp = yield modules_socket.addFace(j["source"], j["target"], as_producer)
            if resp and resp > 0:
                graph.add_edge(j["source"], j["target"], face_id=resp, as_producer=as_producer)
                appendExistingRoutes(j["source"], j["target"])
                return "1"
            else:
                return "0"
        else:
            resp = yield modules_socket.addFace(j["source"], j["target"])
            if resp and resp > 0:
                graph.add_edge(j["source"], j["target"], face_id=resp)
                appendExistingRoutes(j["source"], j["target"])
                return "1"
            else:
                return "0"
    else:
        return "0"


@app.route("/api/links", methods=["GET"])
def sendLinks(request: Request):
    request.responseHeaders.addRawHeader(b"content-type", b"application/json")
    return json.dumps(list(graph.edges), default=jsonSerial)


@app.route("/api/links/<source>/<target>", methods=["GET"])
def getLinkInfo(request: Request, source, target):
    if graph.has_edge(source, target):
        request.setHeader(b"Content-Type", b"application/json")
        return json.dumps(graph.edges[source, target], default=jsonSerial)
    else:
        request.setResponseCode(404)
        return


@app.route("/api/links/<source>/<target>", methods=["DELETE"])
@defer.inlineCallbacks
def removeLink(request: Request, source, target):
    if graph.has_edge(source, target):
        resp = yield modules_socket.delFace(source, target)
        if resp and resp == 1:
            graph.remove_edge(source, target)
            return "1"
        else:
            return "0"
    else:
        return "0"

# API keys ------------------------------------------------------------------------------------------------------------
@app.route("/api/keys", methods=["PUT"])
def newKey(request: Request):
    j = json.loads(request.content.read().decode())
    if (all(field in j for field in ["name", "value"])):
        try:
            keys[j["name"]] = crypto.load_publickey(crypto.FILETYPE_PEM, j["value"])
            addKeyToSVs(j["name"])
        except (ValueError, crypto.Error):
            return "error while parsing the key"
        return "1"
    else:
        return "0"

@app.route("/api/keys", methods=["GET"])
def sendKeysInfo(request: Request):
    return json.dumps(keys, default=jsonSerial)

@app.route("/api/keys", methods=["DELETE"])
def removeKey(request : Request):
    j = json.loads(request.content.read().decode())
    if (all(field in j for field in ["name"])):
        result = keys.pop(j["name"], None)
        if result:
            delSVsKey(j["name"])
            return "1"
        else:
            return "0"
    else:
        return "0"

# modules socket -------------------------------------------------------------------------------------------------------
class ModulesSocket(DatagramProtocol):
    def __init__(self):
        self.routes = {"report": self.handleReport, "request": self.handleRequest, "reply": self.handleReply}
        self.report_routes = {"producer_disconnection": self.handleProducerDisconnectionReport, "cache_status": self.handleCacheStatusReport, "invalid_signature": self.handleInvalidSignatureReport}
        self.request_routes = {"route_registration": self.handlePrefixRegistrationRequest}
        self.reply_results = {"add_face": "face_id", "del_face": "status", "edit_config": "changes", "add_route": "status", "del_route": "status", "add_keys": "status", "del_keys": "status"}
        self.request_counter = 1
        self.pending_requests = {}

    def datagramReceived(self, data, addr):
        try:
            j = json.loads(data.decode())
            self.routes.get(j.get("type", None), self.unknown)(j, addr)
        except ValueError:  # includes simplejson.decoder.JSONDecodeError
            print("[", str(datetime.datetime.now()), "]", "Decoding JSON has failed:", data.decode())

    def unknown(self, data: dict, addr):
        print("[", str(datetime.datetime.now()), "]", json.dumps(data))

    def handleReport(self, j: dict, addr):
        if all(field in j for field in ["name", "action"]):
            self.report_routes.get(j.get("action", None), self.unknown)(j, addr)

    def handleProducerDisconnectionReport(self, j: dict, addr):
        if all(field in j for field in ["face_id"]) and graph.has_node(j["name"]):
            prefixes = list(graph.nodes[j["name"]]["static_routes"].pop(j["face_id"], set()))
            if prefixes:
                propagateDelRoutes(j["name"], prefixes)

    @defer.inlineCallbacks
    def handleCacheStatusReport(self, j: dict, addr):
        if all(field in j for field in ["hit_count", "miss_count"]) and graph.has_node(j["name"]):
            cache_stats = graph.nodes[j["name"]]["cache_stats"]
            hit_delta = j["hit_count"] - cache_stats["hit_count"]
            miss_delta = j["miss_count"] - cache_stats["miss_count"]
            old_ratio = cache_stats["cache_hit"]
            cache_stats["cache_hit"] = round(100 * hit_delta / (hit_delta + miss_delta), 1) if hit_delta + miss_delta > 0 else 0.0
            cache_stats["hit_count"] = j["hit_count"]
            cache_stats["miss_count"] = j["miss_count"]
            cache_stats["last_update"] = time.time()
            print("[", str(datetime.datetime.now()), "] [ handleCacheStatusReport ]", j["name"], "-> cache hit:", cache_stats["cache_hit"])
            try:
                if cache_stats["cache_hit"] < 0.8 * old_ratio:
                    name = j["name"] + ".SV1"
                    if not graph.has_node(name) and createContainer(name, "SV"):
                        graph.add_node(name,  editable=False, scalable=False, addresses=getContainerIPAddresses(name),
                                       **copy.deepcopy(node_default_attrs), **copy.deepcopy(specific_node_default_attrs["SV"]))
                        addKeysToSV(name)
                        yield attachNode(name, [j["name"]], list(graph.successors(j["name"])))
                        lp = LoopingCall(IncreaseSVExpirationCounter, name)
                        lp.start(2)
                        SVs_to_check[name] = lp
            except Exception as e:
                print(e)

    def handleInvalidSignatureReport(self, j: dict, addr):
        print("[", str(datetime.datetime.now()), "] [ handleInvalidSignatureReport ]", json.dumps(j))
        if all(field in j for field in ["invalid_signature_names"]) and graph.has_node(j["name"]):
            packet_stats = graph.nodes[j["name"]]["packet_stats"]
            packet_stats["fake_count"] += len(j["invalid_signature_names"])
            packet_stats["last_update"] = time.time()

    def handleRequest(self, j: dict, addr):
        if all(field in j for field in ["name", "action"]):
            self.request_routes.get(j.get("action", None), self.unknown)(j, addr)

    def handlePrefixRegistrationRequest(self, j: dict, addr):
        # print("[", str(datetime.datetime.now()), "] [ handlePrefixRegistration ]", json.dumps(j))
        if all(field in j for field in ["face_id", "prefix", "key_name", "message", "signature"]) and graph.has_node(j["name"]):
            print("[", str(datetime.datetime.now()), "] [ handlePrefixRegistration ] new route", j["prefix"], "with key", j["key_name"], "via", j["name"])
            result = verifySignature(base64.b64decode(j["message"]), base64.b64decode(j["signature"]), j["key_name"])
            data = {"action": "reply", "id": j.get("id", 0), "result": result}
            self.sendDatagram(data, addr[0], addr[1])
            if result:
                routes = graph.nodes[j["name"]]["static_routes"]
                face_routes = routes.get(j["face_id"], set())
                face_routes.add(j["prefix"])
                routes[j["face_id"]] = face_routes
                propagateNewRoutes(j["name"], [j["prefix"]])

    def handleReply(self, j: dict, addr):
        print("[", str(datetime.datetime.now()), "] [ handleReply ]", json.dumps(j))
        deferred = self.pending_requests.get(j.get("id", 0), None)
        if deferred:
            deferred.callback(j.get(self.reply_results.get(j.get("action", "unknown"), "unknown"), None))
        else:
            print("[", str(datetime.datetime.now()), "]", self.pending_request)

    def editConfig(self, source, data: dict):
        source_addrs = graph.nodes[source]["addresses"]
        d = {"action": "edit_config", "id": self.request_counter}
        d.update(data)
        return self.sendDatagram(d, source_addrs["command"], 10000)

    def addFace(self, source, target, producer=False):
        source_addrs = graph.nodes[source]["addresses"]
        target_addrs = graph.nodes[target]["addresses"]
        is_NR = graph.nodes[target]["type"] == "NR"
        d = {"action": "add_face", "id": self.request_counter, "layer": "tcp", "address": target_addrs["data"], "port": 6362 if is_NR and not producer else 6363}
        return self.sendDatagram(d, source_addrs["command"], 10000)

    def delFace(self, source, target):
        source_addrs = graph.nodes[source]["addresses"]
        face_id = graph.edges[source, target]["face_id"]
        d = {"action": "del_face", "id": self.request_counter, "face_id": face_id}
        return self.sendDatagram(d, source_addrs["command"], 10000)

    def addRoutes(self, name, face_id, prefixes: (list, set)):
        source_addrs = graph.nodes[name]["addresses"]
        d = {"action": "add_route", "id": self.request_counter, "face_id": face_id, "prefixes": prefixes}
        return self.sendDatagram(d, source_addrs["command"], 10000)

    def delRoutes(self, name, face_id, prefix: list):
        source_addrs = graph.nodes[name]["addresses"]
        d = {"action": "del_route", "id": self.request_counter, "face_id": face_id, "prefixes": prefix}
        return self.sendDatagram(d, source_addrs["command"], 10000)

    def addKeys(self, name, keys: (list, set)):
        source_addrs = graph.nodes[name]["addresses"]
        d = {"action": "add_keys", "id": self.request_counter, "keys": keys}
        return self.sendDatagram(d, source_addrs["command"], 10000)

    def delKeys(self, name, keys: (list, set)):
        source_addrs = graph.nodes[name]["addresses"]
        d = {"action": "del_keys", "id": self.request_counter, "keys": keys}
        return self.sendDatagram(d, source_addrs["command"], 10000)

    def list(self, name):
        source_addrs = graph.nodes[name]["addresses"]
        d = {"action": "list", "id": self.request_counter}
        return self.sendDatagram(d, source_addrs["command"], 10000)

    def sendDatagram(self, data: dict, ip, port):
        # print("[", str(datetime.datetime.now()), "]", "send", data, "to", ip, port)
        # deferred to fire when the corresponding reply is received
        d = defer.Deferred()
        d.addTimeout(5, reactor, onTimeoutCancel=self.onTimeout)
        d.addBoth(self.removeRequest, self.request_counter)
        self.pending_requests[self.request_counter] = d
        self.request_counter += 1
        self.transport.write(json.dumps(data, default=jsonSerial).encode(), (ip, port))
        return d

    def onTimeout(self, result, timeout):
        #print("[", str(datetime.datetime.now()), "]", result, timeout)
        return None

    def removeRequest(self, value, key):
        #print("[", str(datetime.datetime.now()), "]", value, key)
        self.pending_requests.pop(key, None)
        return value


modules_socket = ModulesSocket()

# docker ---------------------------------------------------------------------------------------------------------------
docker_client = docker.from_env()


def checkImages():
    print("[", str(datetime.datetime.now()), "]", "check if container images exist...")
    needed_tags = set()
    for e in ["base", "content_store", "backward_router", "name_router", "strategy_router", "signature_verifier"]:
        needed_tags.add("ndn_microservice/" + e + ":latest")
    tags = set()
    for image in docker_client.images.list():
        tags |= set(image.tags)
    if not needed_tags.issubset(tags):
        missing_tags = needed_tags - tags
        print("[", str(datetime.datetime.now()), "]", "building missing image(s): ", missing_tags)
        if "ndn_microservice/base:latest" in missing_tags:
            print("[", str(datetime.datetime.now()), "]", "building base image")
            missing_tags.remove("ndn_microservice/base:latest")
            docker_client.images.build(path="./modules", dockerfile="base", tag="ndn_microservice/base:latest", forcerm=True)
        for tag in missing_tags:
            file = tag.split("/")[1].split(":")[0]
            print("[", str(datetime.datetime.now()), "]", "building", file, "image")
            docker_client.images.build(path="./modules", dockerfile=file, tag=tag, forcerm=True)
        print("[", str(datetime.datetime.now()), "]", "done")
    else:
        print("[", str(datetime.datetime.now()), "]", "ok")


def checkNetworks():
    print("[", str(datetime.datetime.now()), "]", "check if networks exist...")
    needed_networks = {"ndn_microservice_data_network", "ndn_microservice_mgnt_network"}
    networks = set()
    for network in docker_client.networks.list():
        networks.add(network.name)
    if not needed_networks.issubset(networks):
        missing_networks = needed_networks - networks
        print("[", str(datetime.datetime.now()), "]", "create missing network(s): ", missing_networks)
        for network in missing_networks:
            n = docker_client.networks.create(network)
            print("[", str(datetime.datetime.now()), "]", n.attrs)
        print("[", str(datetime.datetime.now()), "]", "done")
    else:
        print("[", str(datetime.datetime.now()), "]", "ok")


def createContainer(name, type) -> bool:
    images = {"CS": "ndn_microservice/content_store", "BR": "ndn_microservice/backward_router",
              "NR": "ndn_microservice/name_router", "SR": "ndn_microservice/strategy_router",
              "SV": "ndn_microservice/signature_verifier"}
    parameters = {"CS": " -s 100000 -p 6363 -C 10000", "BR": " -s 250 -p 6363 -C 10000",
                  "NR": " -c 6362 -p 6363 -C 10000", "SR": " -p 6363 -C 10000",
                  "SV": " -p 6363 -C 10000"}
    try:
        container = docker_client.containers.get(name)
        print("[", str(datetime.datetime.now()), "]", "a unknown container already has the name", name, "-> try remove it")
        container.remove(force=True)
    except docker.errors.NotFound:
        pass
    finally:
        print("[", str(datetime.datetime.now()), "]", "create new container {image: %s, name: %s}" % (images[type], name))
        container = docker_client.containers.create(images[type], name=name, command="-n " + name + parameters[type],
                                                    network="ndn_microservice_data_network", detach=True, cpu_quota=specific_node_default_attrs[type]["cpu_quota"])
        docker_client.networks.get("ndn_microservice_mgnt_network").connect(container)
        container.start()
        container.reload()
        while container.status == "created":
            container.reload()
        return container.status == "running"


def removeContainer(name) -> bool:
    print("[", str(datetime.datetime.now()), "] [ removeContainer ] removing", name)
    try:
        container = docker_client.containers.get(name)
        container.stop(timeout=3)
        container.remove()
        return True
    except (docker.errors.NotFound, docker.errors.APIError):
        return False


def getContainerIPAddresses(name) -> dict:
    try:
        container = docker_client.containers.get(name)
        data_addr = container.attrs["NetworkSettings"]["Networks"]["ndn_microservice_data_network"]["IPAddress"]
        command_addr = container.attrs["NetworkSettings"]["Networks"]["ndn_microservice_mgnt_network"]["IPAddress"]
        return {"data": data_addr, "command": command_addr}
    except docker.errors.NotFound:
        return {}


def getContainerStats(name):
    return docker_client.containers.get(name).stats(stream=False)


def calculate_cpu_percent(d):
    cpu_count = len(d["cpu_stats"]["cpu_usage"]["percpu_usage"])
    cpu_percent = 0.0
    cpu_delta = float(d["cpu_stats"]["cpu_usage"]["total_usage"]) - float(d["precpu_stats"]["cpu_usage"]["total_usage"])
    system_delta = float(d["cpu_stats"]["system_cpu_usage"]) - float(d["precpu_stats"]["system_cpu_usage"])
    if system_delta > 0.0:
        cpu_percent = cpu_delta / system_delta * 100.0 * cpu_count
    return cpu_percent


def calculate_cpu_percent2(d, previous_cpu, previous_system):
    # import json
    # du = json.dumps(d, indent=2)
    # logger.debug("XXX: %s", du)
    cpu_percent = 0.0
    cpu_total = float(d["cpu_stats"]["cpu_usage"]["total_usage"])
    cpu_delta = cpu_total - previous_cpu
    cpu_system = float(d["cpu_stats"]["system_cpu_usage"])
    system_delta = cpu_system - previous_system
    online_cpus = d["cpu_stats"].get("online_cpus", len(d["cpu_stats"]["cpu_usage"]["percpu_usage"]))
    if system_delta > 0.0:
        cpu_percent = (cpu_delta / system_delta) * online_cpus * 100.0
    return cpu_percent, cpu_system, cpu_total


def test2():
    NRs = [name for name, attrs in graph.nodes(data=True) if attrs["type"] == "NR"]
    print("[", str(datetime.datetime.now()), "]", NRs)
    for NR in NRs:
        modules_socket.list(NR)


# ----------------------------------------------------------------------------------------------------------------------
@defer.inlineCallbacks
def peridodicCall():
    yield updateContainersCpuStats()
    yield autoScale()

@defer.inlineCallbacks
def test():
    createContainer("test1", "CS")
    graph.add_node("test1", addresses=getContainerIPAddresses("test1"),
                   **copy.deepcopy(node_default_attrs), **copy.deepcopy(specific_node_default_attrs["CS"]))
    createContainer("test2", "CS")
    graph.add_node("test2", addresses=getContainerIPAddresses("test1"),
                   **copy.deepcopy(node_default_attrs), **copy.deepcopy(specific_node_default_attrs["CS"]))
    createContainer("test3", "CS")
    graph.add_node("test3", addresses=getContainerIPAddresses("test3"),
                   **copy.deepcopy(node_default_attrs), **copy.deepcopy(specific_node_default_attrs["CS"]))
    resp = yield modules_socket.addFace("test1", "test3")
    if resp and resp > 0:
        graph.add_edge("test1", "test3", face_id=resp)
        yield attachNode("test2", ["test1"], ["test3"])
        yield detachNode("test2")

# main -----------------------------------------------------------------------------------------------------------------
if __name__ == "__main__":
    checkImages()
    checkNetworks()
    LoopingCall(updateContainersCpuStats).start(5)
    LoopingCall(autoScale).start(5)
    #reactor.callLater(2, test)
    reactor.listenUDP(9999, modules_socket, maxPacketSize=1 << 16)
    endpoints.serverFromString(reactor, "tcp:8080").listen(Site(app.resource()))  # eq to app.run(...)
    # app.run("0.0.0.0", 8080)
    reactor.suggestThreadPoolSize(5)
    reactor.run()
