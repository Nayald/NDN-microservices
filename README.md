# NDN microservices

This repository holds an experimental microservice architecture implementation of the Named Data Networking protocol.

I propose to split the main functions of a NDN router into multiple microservices and to orchestrate them to make NDN benefit from NFV thanks to this more flexible architecture.

Currently, we have seven microservices: five are usual functions of a NDN router (see NFD), and two are proposed to improve security:

- Name Router (NR): Route Interest packets to producers that have registered a prefix of the name of the packet, it is like the FIB in a NDN router;
- Backward Router (BR): Route back Data packets to the consumers that have asked for it, it is like the PIT in a NDN router;
- Packet Dispatcher (PD): Select the right pipeline for each kind of packet. Since we split PIT and FIB this module is needed for consumer/producer Face, if a client does not need to do both it can directly connect to the right module;
- Content Store (CS): Aims to store Data packets to reuse them later when reasked, like the CS in NDN router;
- Strategy Forwarder (SF): A more general way to apply strategy, unlike NFD, it is not performed after a FIB matching so it can be  placed anywhere (to compensate for the Name Router that only knows multicast routing strategy);
- Signature Verifier (SV): Verify the signature of the NDN packet based on the trusted keys;
- Name Filter (NF): Drop packets based on their name.

We also provide a manager for the microservices, but it is still at an early stage so the code is a bit ugly and some functions are missing . More precisely, it can perform scaling for most of the microservices and deploy a countermeasure against a Content Poisoning Attack based on cache-hit monitoring. It is possible to interact with the manager through a REST API to spawn a microservice, link them, etc... (development will resume soon)

The microservices are in a more mature state and each one can work alone. They do not depend on the manager to work but some advance features can be hard to perform. All microservices implement a management interface. It is used, for example, to change their configuration or to ask them to connect to other endpoints. Some of them can also send some metrics in periodical reports to a given endpoint.

In the current state, the fact to split FIB and PIT is not worth regarding the increased complexity it implies so we will soon add a microservice that fuse Name Route, Backward Router and Packet Dispatcher but this does not mean they are useless (I don't have good example yet). They can still be used as base for new functions like off-path forwarding for Backward Router.
