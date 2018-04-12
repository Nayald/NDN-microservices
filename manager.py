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
        }
    },
    "BR": {
        "type": "BR",
        "size": 250,
    },
    "NR": {
        "type": "NR",
        "static_routes": {},
        "dynamic_routes": {}
    },
    "SR": {
        "type": "SR",
        "strategy": "multicast"
    }
}
locked_nodes = set()

@defer.inlineCallbacks
def propagateNewRoutes(name, prefixes):
    NR_names = [name2 for name2, attrs in graph.nodes(data=True) if attrs["type"] == "NR" and name2 != name]
    for name2 in NR_names:
        for path in nx.all_simple_paths(graph, name2, name):
            #print("[", str(datetime.datetime.now()), "]", path)
            face_id = graph.edges[name2, path[1]]["face_id"]
            resp = yield modules_socket.newRoute(name2, face_id, prefixes)
            if resp and resp == "success":
                propagated_routes = graph.nodes[name2].get("propagated_routes", {})
                routes = propagated_routes.get(face_id, set())
                routes |= prefixes
                propagated_routes[face_id] = routes
                graph.nodes[name2]["propagated_routes"] = propagated_routes
            else:
                print("[", str(datetime.datetime.now()), "]", name2, "-> error while propagate routes")

@defer.inlineCallbacks
def appendExistingRoutes(source, target):
    face_id = graph.edges[source, target]["face_id"]
    NR_names = [name for name, attrs in graph.nodes(data=True) if attrs["type"] == "NR" and name != source]
    prefixes = set()
    for name in NR_names:
        if nx.has_path(graph, target, name):
            for value in graph.nodes[name].get("routes", {}).values():
                prefixes |= value
    if prefixes:
        resp = yield modules_socket.newRoute(source, face_id, prefixes)
        if resp and resp == "success":
            propagated_routes = {face_id: prefixes}
            graph.nodes[source]["propagated_routes"] = propagated_routes
            propagateNewRoutes(source, prefixes)


@defer.inlineCallbacks
def updateContainersCpuStats():
    print("[", str(datetime.datetime.now()), "] [ updateContainersCpuStats ] start")
    nodes = list(graph.nodes(data=True))
    resps = yield defer.DeferredList([threads.deferToThread(getContainerStats, node[0]) for node in nodes], consumeErrors=True)
    for node, resp in zip(nodes, resps):
        cpu_stats = node[1]["cpu_stats"]
        if resp[0]:
            if resp[1]["cpu_stats"].get("system_cpu_usage", None):
                p, s, t = calculate_cpu_percent2(resp[1], cpu_stats["cpu_total"], cpu_stats["cpu_system"])
                cpu_stats["cpu_percent"] = round(p, 1)
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
    print("[", str(datetime.datetime.now()), "] [ updateContainersCpuStats ] end")


@defer.inlineCallbacks
def autoScale():
    print("[", str(datetime.datetime.now()), "] [ autoScale ] start")
    scale_up_functions = {"BR": scaleUpBR, "NR": scaleUpNR}
    scale_down_functions = {"NR": scaleDownNR}
    for name, attrs in list(graph.nodes(data=True)):
        if attrs["scalable"] and name not in locked_nodes:
            locked_nodes.add(name)
            cpu_stats = attrs["cpu_stats"]
            if cpu_stats["cpu_percent"] >= 50 and list(graph.predecessors(name)) and list(graph.successors(name)):
                print("[", str(datetime.datetime.now()), "] [ autoScale ] scale up", name)
                yield scale_up_functions.get(attrs["type"], scaleUp)(name, attrs)
            elif attrs.get("scaled", False) and cpu_stats["cpu_percent"] <= 20:
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
    in_node_list = list(graph.predecessors(name))
    out_node_list = list(graph.successors(name))
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
                # link SR to node
                resp = yield modules_socket.newFace(lb_name, name)
                if resp and resp > 0:
                    print("[", str(datetime.datetime.now()), "] [ scaleUp ]", lb_name, "linked to", name)
                    graph.add_edge(lb_name, name, face_id=resp)
                    # for each predecessor
                    for in_name in in_node_list:
                        # link predecessor to SR
                        resp = yield modules_socket.newFace(in_name, lb_name)
                        if resp and resp > 0:
                            print("[", str(datetime.datetime.now()), "] [ scaleUp ]", in_name, "linked to", lb_name)
                            graph.add_edge(in_name, lb_name, face_id=resp)
                            # unlink predecessor and node
                            resp = yield modules_socket.delFace(in_name, name)
                            if resp and resp > 0:
                                print("[", str(datetime.datetime.now()), "] [ scaleUp ]", in_name, "no longer linked to", name)
                                graph.remove_edge(in_name, name)
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
        for out_name in out_node_list:
            # link clone to successor
            resp = yield modules_socket.newFace(clone_name, out_name)
            if resp and resp > 0:
                print("[", str(datetime.datetime.now()), "] [ scaleUp ]", clone_name, "linked to", out_name)
                graph.add_edge(clone_name, out_name, face_id=resp)
        # link clone to SR
        resp = yield modules_socket.newFace(lb_name, clone_name)
        if resp and resp > 0:
            print("[", str(datetime.datetime.now()), "] [ scaleUp ]", lb_name, "linked to", clone_name)
            graph.add_edge(lb_name, clone_name, face_id=resp)
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
                resp = yield modules_socket.newFace(lb_name, name)
                if resp and resp > 0:
                    print("[", str(datetime.datetime.now()), "] [ scaleUpBR ]", lb_name, "linked to", name)
                    graph.add_edge(lb_name, name, face_id=resp)
                    lb_nodes_list.append(lb_name)
                    resp = yield modules_socket.newFace(in_node_list[i], lb_name)
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
            resp = yield modules_socket.newFace(clone_name, out_name)
            if resp and resp > 0:
                print("[", str(datetime.datetime.now()), "] [ scaleUpBR ]", clone_name, "linked to", out_name)
                graph.add_edge(clone_name, out_name, face_id=resp)
        for lb_name in lb_nodes_list:
            resp = yield modules_socket.newFace(lb_name, clone_name)
            if resp and resp > 0:
                print("[", str(datetime.datetime.now()), "] [ scaleUpBR ]", lb_name, "linked to", clone_name)
                graph.add_edge(lb_name, clone_name, face_id=resp)
    attrs["scale"] = scale + 1
    attrs["scaled"] = True
    print("[", str(datetime.datetime.now()), "] [ scaleUpBR ] end")


def scaleUpNR(name, attrs):
    print("[", str(datetime.datetime.now()), "] [ scaleUpNR ] pass")
    pass


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
                resp = yield modules_socket.newFace(in_name, name)
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


# API ------------------------------------------------------------------------------------------------------------------
app = Klein()

def jsonSerial(obj):
    """JSON serializer for objects not serializable by default json code"""

    if isinstance(obj, set):
        return list(obj)
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
def createNode(request: Request):
    j = json.loads(request.content.read().decode())
    if all(field in j for field in ["type"]) and j["type"] in graph_counters:
        name = j["type"] + str(graph_counters[j["type"]])
        graph_counters[j["type"]] += 1
        if createContainer(name, j["type"]):
            graph.add_node(name, editable=True, scalable=True, addresses=getContainerIPAddresses(name),
                           **copy.deepcopy(node_default_attrs), **copy.deepcopy(specific_node_default_attrs[j["type"]]))
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
                in_nodes = list(graph.predecessors(name))
                out_nodes = list(graph.successors(name))
                for out_name in out_nodes:
                    for in_name in in_nodes:
                        resp = yield modules_socket.newFace(in_name, out_name)
                        if resp and resp > 0:
                            print("[", str(datetime.datetime.now()), "] [ removeNode ] link", in_name, "to", out_name)
                            graph.add_edge(in_name, out_name, face_id=resp)
                            resp = yield modules_socket.delFace(in_name, name)
                            if resp:
                                print("[", str(datetime.datetime.now()), "] [ removeNode ] unlink", in_name, "and", name)
                                graph.remove_edge(in_name, name)
                    resp = yield modules_socket.delFace(name, out_name)
                    if resp:
                        print("[", str(datetime.datetime.now()), "] [ removeNode ] unlink", name, "and", out_name)
                        graph.remove_edge(name, out_name)
                resp = yield threads.deferToThread(removeContainer, name)
                if resp:
                    print("[", str(datetime.datetime.now()), "] [ removeNode ]", name, "removed")
                    graph.remove_node(name)
                locked_nodes.remove(name)
        except Exception as e:
            print(e)
        else:
            request.setResponseCode(503)
            return name + " is not editable or locked"
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
        resp = yield modules_socket.newFace(j["source"], j["target"])#, j.get("producer", False) if graph.nodes[j["target"]]["type"] == "NR" else False)
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


# modules socket -------------------------------------------------------------------------------------------------------
class ModulesSocket(DatagramProtocol):
    def __init__(self):
        self.routes = {"report": self.handleReport, "request": self.handleRequest, "reply": self.handleReply}
        self.request_routes = {"route_registration": self.handlePrefixRegistration}
        self.reply_results = {"add_face": "face_id", "del_face": "status", "edit_config": "changes", "add_route": "status"}
        self.request_counter = 1
        self.pending_requests = {}

    def datagramReceived(self, data, addr):
        try:
            j = json.loads(data.decode())
            self.routes.get(j.get("type", None), self.unknown)(j, addr)
        except ValueError:  # includes simplejson.decoder.JSONDecodeError
            print("[", str(datetime.datetime.now()), "]", "Decoding JSON has failed:", data.decode())

    def unknown(self, j: dict, addr):
        print("[", str(datetime.datetime.now()), "]", json.dumps(j))

    def handleReport(self, j: dict, addr):
        node = graph.nodes.get(j["name"], None)
        if graph.has_node(j["name"]):
            cache_stats = graph.nodes[j["name"]]["cache_stats"]
            hits = j["hit_count"] - cache_stats["hit_count"]
            misses = j["miss_count"] - cache_stats["miss_count"]
            if hits <= 0 and misses <= 0:
                hits = j["hit_count"]
                misses = j["miss_count"]
            if hits == 0:
                cache_stats["cache_hit"] = 0.0
            else:
                cache_stats["cache_hit"] = round(100 * hits / (hits + misses), 1)
            cache_stats["hit_count"] = j["hit_count"]
            cache_stats["miss_count"] = j["miss_count"]
            cache_stats["last_update"] = time.time()
        else:
            print("[", str(datetime.datetime.now()), "]", "unknown node name")

    def handleRequest(self, j: dict, addr):
        if all(field in j for field in ["name", "action"]):
            self.request_routes.get(j.get("action", None), self.unknown)(j, addr)

    def handlePrefixRegistration(self, j: dict, addr):
        print("[", str(datetime.datetime.now()), "] [ handlePrefixRegistration ]", json.dumps(j))
        if all(field in j for field in ["face_id", "prefix"]) and graph.has_node(j.get("name", None)):
            data = {"action": "reply", "id": j.get("id", 0), "result": True}
            self.sendDatagram(data, addr[0], addr[1])
            routes = graph.nodes[j["name"]].get("routes", {})
            face_routes = routes.get(j["face_id"], set())
            face_routes.add(j["prefix"])
            routes[j["face_id"]] = face_routes
            graph.nodes[j["name"]]["routes"] = routes
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
        json = {"action": "edit_config", "id": self.request_counter}
        json.update(data)
        return self.sendDatagram(json, source_addrs["command"], 10000)

    def newFace(self, source, target, port=6363):
        source_addrs = graph.nodes[source]["addresses"]
        target_addrs = graph.nodes[target]["addresses"]
        json = {"action": "add_face", "id": self.request_counter, "layer": "tcp", "address": target_addrs["data"], "port": 6362 if graph.nodes[target]["type"] == "NR" else 6363}
        return self.sendDatagram(json, source_addrs["command"], 10000)

    def delFace(self, source, target):
        source_addrs = graph.nodes[source]["addresses"]
        face_id = graph.edges[source, target]["face_id"]
        json = {"action": "del_face", "id": self.request_counter, "face_id": face_id}
        return self.sendDatagram(json, source_addrs["command"], 10000)

    def newRoute(self, source, face_id, prefixes: (list, set)):
        source_addrs = graph.nodes[source]["addresses"]
        json = {"action": "add_route", "id": self.request_counter, "face_id": face_id, "prefixes": prefixes}
        return self.sendDatagram(json, source_addrs["command"], 10000)

    def delRoute(self, source, face_id, prefix: str):
        source_addrs = graph.nodes[source]["addresses"]
        json = {"action": "del_route", "id": self.request_counter, "face_id": face_id, "prefix": prefix}
        return self.sendDatagram(json, source_addrs["command"], 10000)

    def list(self, source):
        source_addrs = graph.nodes[source]["addresses"]
        json = {"action": "list", "id": self.request_counter}
        return self.sendDatagram(json, source_addrs["command"], 10000)

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
    for e in ["base", "content_store", "backward_router", "name_router", "strategy_router"]:
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
              "NR": "ndn_microservice/name_router", "SR": "ndn_microservice/strategy_router"}
    parameters = {"CS": " -s 100000 -p 6363 -C 10000", "BR": " -s 250 -p 6363 -C 10000",
                  "NR": " -c 6362 -p 6363 -C 10000", "SR": " -p 6363 -C 10000"}
    try:
        container = docker_client.containers.get(name)
        print("[", str(datetime.datetime.now()), "]", "a unknown container already has the name", name, "-> try remove it")
        container.remove(force=True)
    except docker.errors.NotFound:
        pass
    finally:
        print("[", str(datetime.datetime.now()), "]", "create new container {image: %s, name: %s}" % (images[type], name))
        container = docker_client.containers.create(images[type], name=name, command="-n " + name + parameters[type],
                                                    network="ndn_microservice_data_network", detach=True, cpu_quota=100000)
        docker_client.networks.get("ndn_microservice_mgnt_network").connect(container)
        container.start()
        container.reload()
        while container.status == "created":
            container.reload()
        return container.status == "running"


def removeContainer(name) -> bool:
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


# main -----------------------------------------------------------------------------------------------------------------
if __name__ == "__main__":
    checkImages()
    checkNetworks()
    #LoopingCall(test2).start(5)
    LoopingCall(peridodicCall).start(10)
    reactor.listenUDP(9999, modules_socket, maxPacketSize=1 << 16)
    endpoints.serverFromString(reactor, "tcp:8080").listen(Site(app.resource()))  # eq to app.run(...)
    # app.run("0.0.0.0", 8080)
    reactor.suggestThreadPoolSize(5)
    reactor.run()
