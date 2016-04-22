#include "veins/modules/application/ddash/DDash11p.h"

using Veins::TraCIMobilityAccess;
using Veins::AnnotationManagerAccess;

Define_Module(DDash11p);

void DDash11p::initialize(int stage) {
	BaseWaveApplLayer::initialize(stage);
    if (stage == 0) {
        mobility = TraCIMobilityAccess().get(getParentModule());
        traci = mobility->getCommandInterface();
        traciVehicle = mobility->getVehicleCommandInterface();
        annotations = AnnotationManagerAccess().getIfExists();
        ASSERT(annotations);
        sentMessage = false;
        lastDroveAt = simTime();

        //simulate asynchronous channel access
        double maxOffset = par("maxOffset").doubleValue();
        double offSet = dblrand() * (par("protocolPeriod").doubleValue()/2);
        offSet = offSet + floor(offSet/0.050)*0.050;
        individualOffset = dblrand() * maxOffset;

        //Schedule the first heartbeat messages
        heartbeatMsg = new cMessage((mobility->getExternalId()).c_str(), HEARTBEAT);
        scheduleAt(simTime() + offSet, heartbeatMsg);

        //Timeout values
        timeout = par("timeoutPeriod").doubleValue();
        period = par("protocolPeriod").doubleValue();
    }
}


/**********************************************************************************
 *
 * SEND METHODS
 *
 **********************************************************************************/
void DDash11p::sendWSM(WaveShortMessage* wsm) {
	sendDelayedDown(wsm,individualOffset);
}


void DDash11p::sendJoin(){
    WaveShortMessage *wsm;
    NodeMsgs joinList;
    joinList.push_front(getMyName());

    wsm = prepareWSM(getMyName(), beaconLengthBits, type_CCH, beaconPriority, 0, -1);
    wsm->setKind(PING);
    wsm->setJoinMsgs(joinList);
    wsm->setSrc(getMyName().c_str());
    wsm->setDst("*");
    wsm->setGroup(mobility->getRoadId().c_str());

    sendWSM(wsm);
    if(!sentJoinDbgMsg)
    {
        debug("JOIN");
        sentJoinDbgMsg = true;
    }
}


void DDash11p::sendPing(const char* node){
    WaveShortMessage *wsm;

    wsm = prepareWSM("", beaconLengthBits, type_CCH, beaconPriority, 0, -1);
    wsm->setKind(PING);
    setUpdateMsgs(wsm);

    wsm->setSrc(getMyName().c_str());
    wsm->setDst(node);
    wsm->setGroup(getGroup().c_str());
    sendWSM(wsm);

    nodeMap[std::string(node)] = PINGWAIT;

    debug("PING " + std::string(node));
}


void DDash11p::sendPingReq(std::string nodeName){
    int kNodesMax = par("pingReqNum");
    int kNodes = 0;
    int nodeIdx;
    int numNodes = nodeMap.size();
    WaveShortMessage *wsm;
    std::string middleNode;

    if(numNodes - kNodesMax > 1) {

        while(kNodes < kNodesMax) {

            /* Select random node for middle man node */
            nodeIdx = rand() % numNodes;
            middleNode = nodeList[nodeIdx];

            if(middleNode != nodeName && nodeMap[middleNode] == ALIVE) {
                wsm = prepareWSM("", beaconLengthBits, type_CCH, beaconPriority, 0, -1);
                wsm->setKind(PINGREQ);
                setUpdateMsgs(wsm);

                wsm->setSrc(getMyName().c_str());
                wsm->setDst(middleNode.c_str());
                wsm->setGroup(getGroup().c_str());
                wsm->setWsmData(nodeName.c_str());

                sendWSM(wsm);

                nodeMap[middleNode] = PINGREQWAIT;

                setTimer(getMyName().c_str(), middleNode.c_str(), nodeName.c_str());
                kNodes++;

                debug("Sent PINGREQ to " + middleNode + " for " + nodeName);
            }

        }

    } else {
        debug("Not enough nodes for K");
    }
}


void DDash11p::sendAck(std::string dst) {
    WaveShortMessage* wsm = prepareWSM("ACK", beaconLengthBits, type_CCH, beaconPriority, 0, -1);
    wsm->setKind(ACK);
    wsm->setSrc(getMyName().c_str());
    wsm->setDst(dst.c_str());
    wsm->setWsmData("");
    wsm->setGroup(getGroup().c_str());
    sendWSM(wsm);
    debug("send ack to " + dst);
}


void DDash11p::sendAck(std::string src, std::string dst, std::string data) {
    WaveShortMessage* wsm = prepareWSM("PINGREQ_ACK", beaconLengthBits, type_CCH, beaconPriority, 0, -1);
    wsm->setKind(ACK);
    wsm->setSrc(src.c_str());
    wsm->setDst(dst.c_str());
    wsm->setWsmData(data.c_str());
    wsm->setGroup(getGroup().c_str());
    sendWSM(wsm);
    debug("send ack to ping request to " + dst + " from " + data);
}


void DDash11p::forwardAck(WaveShortMessage* wsm) {}

//TODO need to remove this and add the node to leaveMsgs instead
void DDash11p::sendFail(std::string nodeName) {
    WaveShortMessage* wsm = prepareWSM("", beaconLengthBits, type_CCH, beaconPriority, 0, -1);
    wsm->setKind(FAIL);
    wsm->setDst("*");
    wsm->setWsmData(nodeName.c_str());
    sendWSM(wsm);
    debug("Fail " + nodeName);
}


void DDash11p::sendMessage(std::string blockedRoadId) {
    sentMessage = true;
    t_channel channel = dataOnSch ? type_SCH : type_CCH;
    WaveShortMessage* wsm = prepareWSM("data", dataLengthBits, channel, dataPriority, -1,2);
    wsm->setWsmData(blockedRoadId.c_str());
    sendWSM(wsm);
}


/**********************************************************************************
 *
 * RECEIVE METHODS
 *
 **********************************************************************************/

void DDash11p::onPing(WaveShortMessage* wsm){
    if(isMyGroup(wsm) && isForMe(wsm)) {
        std::string sender = std::string(wsm->getSrc());
        debug("PING from " + sender);
        saveNodeInfo(wsm);
        getUpdateMsgs(wsm);
        sendAck(sender);
    }
}


void DDash11p::onPingReq(WaveShortMessage* wsm) {
    if(isMyGroup(wsm) && isForMe(wsm)) {
        debug("PING REQ received");
        saveNodeInfo(wsm);
        getUpdateMsgs(wsm);
        setTimer(wsm->getSrc(), wsm->getWsmData(), "");
        sendPing(wsm->getWsmData());
        pingReqSent[wsm->getWsmData()] = wsm->getSrc();
    }
}


void DDash11p::onAck(WaveShortMessage* wsm){
    std::string src = std::string(wsm->getSrc());
    std::string data = std::string(wsm->getWsmData());

    if(isMyGroup(wsm) && isForMe(wsm)) {
        debug("ACK from " + src + ", data: " + data);

        if(!hasNode(src)) {
            addNode(src.c_str());
        } else {
            nodeMap[src] = ALIVE;
        }

        if(data != "") {
            nodeMap[data] = ALIVE;
        }

        if(isPingReqAck(src)) {
            sendAck(getMyName(), pingReqSent[src], src);
            pingReqSent.erase(src);
        }
    } else {
        debug("Ignore ack for " + std::string(wsm->getDst()));
    }
}

void DDash11p::onFail(WaveShortMessage* wsm) {
    std::string failName = std::string(wsm->getWsmData());
    if(hasNode(failName)) {
        nodeMap.erase(failName);
        removeFromList(failName);
        debug("Delete: " + failName);
    }
}

void DDash11p::onBeacon(WaveShortMessage* wsm) {
    if(flashOn) {
        findHost()->getDisplayString().updateWith("r=16,red");
        flashOn = false;
    } else {
        findHost()->getDisplayString().updateWith("r=16,green");
        flashOn = true;
    }
}


void DDash11p::onData(WaveShortMessage* wsm) {
    findHost()->getDisplayString().updateWith("r=16,green");
}


void DDash11p::receiveSignal(cComponent* source, simsignal_t signalID, cObject* obj) {
    Enter_Method_Silent();
    if (signalID == mobilityStateChangedSignal) {
        handlePositionUpdate(obj);
    }
}


/**********************************************************************************
 *
 * HANDLER METHODS
 *
 **********************************************************************************/

void DDash11p::handleSelfMsg(cMessage* msg) {
    std::string dst;
    WaveShortMessage *wsm;

    switch (msg->getKind()) {
        case HEARTBEAT:
            if(nodeMap.empty()) {
                sendJoin();
            } else {

                const char* nodeName = getNextNode();
                if(nodeName == nullptr) {
                    if(!sentNoOneDbgMsg) {
                        debug("No one to ping");
                        sentNoOneDbgMsg = true;
                    }
                    scheduleAt(simTime() + par("beaconInterval").doubleValue(), heartbeatMsg);
                    return;
                }

                setTimer(getMyName().c_str(), nodeName, "");
                sendPing(nodeName);
                sentNoOneDbgMsg = false;
            }

            scheduleAt(simTime() + par("beaconInterval").doubleValue(), heartbeatMsg);
            break;

        case TIMEOUT:
            wsm = check_and_cast<WaveShortMessage*>(msg);
            dst = std::string(wsm->getDst());

            if(nodeMap[dst] == PINGWAIT) {
                debug("Timeout on PINGWAIT for " + dst);
                setTimer(getMyName().c_str(), wsm->getDst(), "");
                sendPingReq(dst);
                nodeMap[dst] = PINGWAIT2;
            } else if(nodeMap[dst] == PINGWAIT2) {
                debug("Timeout on PINGWAIT2. Fail(" + dst + ")");
                failNode(dst);
            } else if(nodeMap[dst] == PINGREQWAIT) {
                debug("Timeout on PINGREQ. Resetting " + dst);
                nodeMap[dst] = ALIVE;
            }
            break;

        default:
            if (msg)
                DBG << "APP: Error: Got Self Message of unknown kind! Name: " << msg->getName() << endl;
            break;
    }
}


void DDash11p::handlePositionUpdate(cObject* obj) {
    BaseWaveApplLayer::handlePositionUpdate(obj);
    if(roadChanged()) {
        debug("Leaving group " + groupName);
        groupName = mobility->getRoadId();
        leaveMsgs.clear();
        joinMsgs.clear();
    }
}


/**********************************************************************************
 *
 * STATE METHODS
 *
 **********************************************************************************/
void DDash11p::saveNodeInfo(WaveShortMessage *wsm) {
    const char* sender = wsm->getSrc();
    if(!hasNode(sender)) {
        debug("Sender is new: " + std::string(sender));
        addNode(sender);
    }
}


void DDash11p::failNode(std::string name) {
    debug("Fail(" + name + ")");
    leaveMsgs[name]++;
    removeFromList(name);
    nodeMap.erase(name);
}


void DDash11p::addNode(const char* name) {
    if(!hasNode(name)) {
        debug("addNode " + std::string(name));
        nodeList.push_back(std::string(name));
    }

    nodeMap[std::string(name)] = ALIVE;

    if(nodeMap.size() == 1) {
        mapIter = nodeMap.begin();
        lastIdx = 0;
    }
}


const char* DDash11p::getNextNode() {
    std::string next;
    size_t count = 0;
    do {
        if(count == nodeMap.size()) {
            next = "";
            if(lastIdx == nodeMap.size()) {
                lastIdx = 0;
            }
            return nullptr;
        }
        next = nodeList[lastIdx];
        lastIdx++;
        count++;
    } while(nodeMap[next] != ALIVE);

    if(lastIdx == nodeMap.size()) {
        lastIdx = 0;
    }
    return next.c_str();
}


void DDash11p::setTimer(const char* src, const char* dst, const char* data) {
    WaveShortMessage* msg = new WaveShortMessage("", TIMEOUT);
    msg->setSrc(src);
    msg->setDst(dst);
    msg->setWsmData(data);
    pingReqTimers[std::string(src)][std::string(dst)] = msg;
    scheduleAt(simTime() + timeout, msg);
}


void DDash11p::removeFromList(std::string name) {
    debug("Remove NodeList: " + name);
    for (size_t i = 0; i < nodeList.size(); i++) {
        if (name == nodeList[i]) {

            nodeList.erase(nodeList.begin() + i);
            return;
        }
    }
}


void DDash11p::setUpdateMsgs(WaveShortMessage* wsm) {
    NodeMsgs msgs;

    for(NodeMap::iterator it=joinMsgs.begin(); it!=joinMsgs.end(); it++) {
        msgs.push_front(it->first);
    }
    wsm->setJoinMsgs(msgs);

    msgs.clear();

    for(NodeMap::iterator it=leaveMsgs.begin(); it!=leaveMsgs.end(); it++) {
        msgs.push_front(it->first);
    }
    wsm->setLeaveMsgs(msgs);
}


void DDash11p::getUpdateMsgs(WaveShortMessage* wsm) {
    NodeMsgs joins = wsm->getJoinMsgs();
    NodeMsgs leaves = wsm->getLeaveMsgs();
    for(std::string s: joins) {
        debug("Update= join(" + s + ")");
        joinMsgs[s]++;
        if(joinMsgs[s] >= joinMax) {
            joinMaxes.push_back(s);
            joinMax = joinMsgs[s];
        }

        if(!isMyName(s) && !hasNode(s)) {
             debug("New node " + s);
             addNode(s.c_str());
         }
    }

    for(std::string s: leaves) {
        leaveMsgs[s]++;
        debug("Update= leave(" + s + ")");
        if(leaveMsgs[s] >= leaveMax) {
            leaveMaxes.push_back(s);
            leaveMax = leaveMsgs[s];
        }
        removeFromList(s);
        nodeMap.erase(s);
    }

    if(leaveMsgs.size() > LRU_SIZE) {
        leaveMsgs.erase(leaveMaxes.back());
        leaveMaxes.pop_back();
    }

    if(joinMsgs.size() > LRU_SIZE) {
        joinMsgs.erase(joinMaxes.back());
        joinMaxes.pop_back();
    }
}
