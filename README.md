## Introduction

This is a simple p2p file sharing system.

## Design explaination

### How all peers establish connection
On start up, every peer connects to a known register server(ip of the register server is pecified in settings.config). 
When all peers are connected, register server broadcast ip of all peers to every peer. After this, all peers be connected to peers specified in corresponding topology config files.

### Query
A peer can issue a Query message to all of its connected peers.
When a Query message is received, if the receiver has the file, it sends back a QueryHit, otherwise it pass down the Query message.

### QueryHit
Every receiver of Query will record the upstream sender of the Query message. So when a QueryHit is issued, the QueryHit will be passed back in the exact way the Query message is passed down.

### Obtain(filename)
Obtain is done in two steps.
The first step is to send a Query message and wait for QueryHit.
When a QueryHit is received, issueing peer checks if it is waiting for a MessageID, if it is true, it establish a connection with the peer that sends the QueryHit to start downloading file. (QueryHit contains the ip and port information of the sender of QueryHit)

### Replication
When all peers are connected, each peer will start do file replication. A InvQuery will be send to all neighbour peers. InvQuery is similar to Query, the difference is that filename will be recorded so that a peer node will only deal with the replication of a file once, and a InvQueryHit will be send back only if the specified file is not found.

### How do peer handle intensive query/obtain simultaneously
I use a queue to store all the requests, and worker thread uses a condition variable to wait on requests. When the message queue enques some request, it grabs the condition variable and call notify, then worker thread retrive request from the queue and process it. This way worker thread can deal with instensive amount of requests efficiently.

## How to run the program
0. Setup register_server's ip, replication_factor in settings.config
1. execute './gen-dataset.sh {prefix}' to generate dataset files.
2. Go to `src` directory, execute `make`
3. 'register_server' and 'peer' executables will be created
4. Copy necessary files to every virtual machine:gen-dataset.sh, peer, settings.config, top_mesh_2d.config, top_star.config
5. Start 'register_server' with command './register {peer_number}', {peer_number} needs to be the actual number of peers.
6. Start 'peer' with command './peer {ETopologyType} {peer_id}'.
ETopologyType: 0 for 3 node testing, 1 for 16 nodes star, 2 for 16 nodes 2d mesh.
7. When all peers are started, they will automaticlly be connected based on the topology config files.