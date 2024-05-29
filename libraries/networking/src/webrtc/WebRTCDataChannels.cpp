//
//  WebRTCDataChannels.cpp
//  libraries/networking/src/webrtc
//
//  Created by David Rowe on 21 May 2021.
//  Copyright 2021 Vircadia contributors.
//  Copyright 2021 DigiSomni LLC.
//

#include "WebRTCDataChannels.h"

#if defined(WEBRTC_DATA_CHANNELS)

#include <QProcessEnvironment>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

#include "../NetworkLogging.h"


// References:
// - https://webrtc.github.io/webrtc-org/native-code/native-apis/
// - https://webrtc.googlesource.com/src/+/master/api/peer_connection_interface.h

// FIXME: stun:ice.vircadia.com:7337 doesn't work for WebRTC.
// Firefox warns: "WebRTC: Using more than two STUN/TURN servers slows down discovery"
const std::list<std::string> DEFAULT_ICE_SERVER_URLS = {
    "stun:stun1.l.google.com:19302",
    "stun:stun.schlund.de"
};
const int MAX_WEBRTC_BUFFER_SIZE = 16777216;  // 16MB

#define WEBRTC_DEBUG_LEVEL_NONE 0 // default value if left undefined
#define WEBRTC_DEBUG_LEVEL_NORMAL 1 // basic peer connection logs
#define WEBRTC_DEBUG_LEVEL_VERBOSE 2 // full data channel logs

// #define WEBRTC_DEBUG WEBRTC_DEBUG_LEVEL_NORMAL

using namespace webrtc;


void WDCSetSessionDescriptionObserver::OnSuccess() {
#if WEBRTC_DEBUG >= WEBRTC_DEBUG_LEVEL_VERBOSE
    qCDebug(networking_webrtc) << "WDCSetSessionDescriptionObserver::OnSuccess()";
#endif
}

void WDCSetSessionDescriptionObserver::OnFailure(RTCError error) {
#if WEBRTC_DEBUG >= WEBRTC_DEBUG_LEVEL_VERBOSE
    qCDebug(networking_webrtc) << "WDCSetSessionDescriptionObserver::OnFailure() :" << error.message();
#endif
}


// NOTE: this indicates a newer WebRTC build in use on linux,
// TODO: remove the old code when windows WebRTC is updated as well
#ifdef API_SET_LOCAL_DESCRIPTION_OBSERVER_INTERFACE_H_

void WDCSetLocalDescriptionObserver::OnSetLocalDescriptionComplete(webrtc::RTCError error) {
#if WEBRTC_DEBUG >= WEBRTC_DEBUG_LEVEL_VERBOSE
    if(error.ok()) {
        qCDebug(networking_webrtc) << "SetLocalDescription call succeeded";
    } else {
        qCDebug(networking_webrtc) << "SetLocalDescription call failed:" << error.message();
    }
#endif
}

void WDCSetRemoteDescriptionObserver::OnSetRemoteDescriptionComplete(webrtc::RTCError error) {
#if WEBRTC_DEBUG >= WEBRTC_DEBUG_LEVEL_VERBOSE
    if(error.ok()) {
        qCDebug(networking_webrtc) << "SetRemoteDescription call succeeded";
    } else {
        qCDebug(networking_webrtc) << "SetRemoteDescription call failed:" << error.message();
    }
#endif
}

#endif

WDCCreateSessionDescriptionObserver::WDCCreateSessionDescriptionObserver(WDCConnection* parent) :
    _parent(parent)
{ }

void WDCCreateSessionDescriptionObserver::OnSuccess(SessionDescriptionInterface* descriptionRaw) {
#if WEBRTC_DEBUG >= WEBRTC_DEBUG_LEVEL_VERBOSE
    qCDebug(networking_webrtc) << "WDCCreateSessionDescriptionObserver::OnSuccess()";
#endif
    // NOTE: according to documentation in the relevant webrtc header,
    // ownership of description is transferred here, so we use a unique pointer
    // to accept and transfer it further. The callback signature is likely to
    // change in the future to pass a unique pointer.
    auto description = std::unique_ptr<SessionDescriptionInterface>(descriptionRaw);
    std::string descriptionString;
    description->ToString(&descriptionString);
    _parent->sendAnswer(descriptionString);
    _parent->setLocalDescription(std::move(description));
}

void WDCCreateSessionDescriptionObserver::OnFailure(RTCError error) {
#if WEBRTC_DEBUG >= WEBRTC_DEBUG_LEVEL_VERBOSE
    qCDebug(networking_webrtc) << "WDCCreateSessionDescriptionObserver::OnFailure() :" << error.message();
#endif
}


WDCPeerConnectionObserver::WDCPeerConnectionObserver(WDCConnection* parent) :
    _parent(parent)
{ }

void WDCPeerConnectionObserver::OnSignalingChange(PeerConnectionInterface::SignalingState newState) {
#if WEBRTC_DEBUG >= WEBRTC_DEBUG_LEVEL_NORMAL
    QStringList states {
        "Stable",
        "HaveLocalOffer",
        "HaveLocalPrAnswer",
        "HaveRemoteOffer",
        "HaveRemotePrAnswer",
        "Closed"
    };
    qCDebug(networking_webrtc) << "WDCPeerConnectionObserver::OnSignalingChange() :" << newState << states[newState];
#endif
}

void WDCPeerConnectionObserver::OnRenegotiationNeeded() {
#if WEBRTC_DEBUG >= WEBRTC_DEBUG_LEVEL_VERBOSE
    qCDebug(networking_webrtc) << "WDCPeerConnectionObserver::OnRenegotiationNeeded()";
#endif
}

void WDCPeerConnectionObserver::OnIceGatheringChange(PeerConnectionInterface::IceGatheringState newState) {
#if WEBRTC_DEBUG >= WEBRTC_DEBUG_LEVEL_NORMAL
    QStringList states {
        "New",
        "Gathering",
        "Complete"
    };
    qCDebug(networking_webrtc) << "WDCPeerConnectionObserver::OnIceGatheringChange() :" << newState << states[newState];
#endif
}

void WDCPeerConnectionObserver::OnIceCandidate(const IceCandidateInterface* candidate) {
#if WEBRTC_DEBUG >= WEBRTC_DEBUG_LEVEL_NORMAL
    qCDebug(networking_webrtc) << "WDCPeerConnectionObserver::OnIceCandidate()";
#endif
    _parent->sendIceCandidate(candidate);
}

void WDCPeerConnectionObserver::OnIceConnectionChange(PeerConnectionInterface::IceConnectionState newState) {
#if WEBRTC_DEBUG >= WEBRTC_DEBUG_LEVEL_NORMAL
    QStringList states {
        "New",
        "Checking",
        "Connected",
        "Completed",
        "Failed",
        "Disconnected",
        "Closed",
        "Max"
    };
    qCDebug(networking_webrtc) << "WDCPeerConnectionObserver::OnIceConnectionChange() :" << newState << states[newState];
#endif
}

void WDCPeerConnectionObserver::OnStandardizedIceConnectionChange(PeerConnectionInterface::IceConnectionState newState) {
#if WEBRTC_DEBUG >= WEBRTC_DEBUG_LEVEL_NORMAL
    QStringList states {
        "New",
        "Checking",
        "Connected",
        "Completed",
        "Failed",
        "Disconnected",
        "Closed",
        "Max"
    };
    qCDebug(networking_webrtc) << "WDCPeerConnectionObserver::OnStandardizedIceConnectionChange() :" << newState
        << states[newState];
#endif
}

void WDCPeerConnectionObserver::OnDataChannel(rtc::scoped_refptr<DataChannelInterface> dataChannel) {
#if WEBRTC_DEBUG >= WEBRTC_DEBUG_LEVEL_NORMAL
    qCDebug(networking_webrtc) << "WDCPeerConnectionObserver::OnDataChannel()";
#endif
    _parent->onDataChannelOpened(dataChannel);
}

void WDCPeerConnectionObserver::OnConnectionChange(PeerConnectionInterface::PeerConnectionState newState) {
#if WEBRTC_DEBUG >= WEBRTC_DEBUG_LEVEL_NORMAL
    QStringList states {
        "New",
        "Connecting",
        "Connected",
        "Disconnected",
        "Failed",
        "Closed"
    };
    qCDebug(networking_webrtc) << "WDCPeerConnectionObserver::OnConnectionChange() :" << (uint)newState
        << states[(uint)newState];
#endif
    _parent->onPeerConnectionStateChanged(newState);
}


WDCDataChannelObserver::WDCDataChannelObserver(WDCConnection* parent) :
    _parent(parent)
{ }

void WDCDataChannelObserver::OnStateChange() {
#if WEBRTC_DEBUG >= WEBRTC_DEBUG_LEVEL_NORMAL
    qCDebug(networking_webrtc) << "WDCDataChannelObserver::OnStateChange()";
#endif
    _parent->onDataChannelStateChanged();
}

void WDCDataChannelObserver::OnMessage(const DataBuffer& buffer) {
#if WEBRTC_DEBUG >= WEBRTC_DEBUG_LEVEL_VERBOSE
    qCDebug(networking_webrtc) << "WDCDataChannelObserver::OnMessage()";
#endif
    _parent->onDataChannelMessageReceived(buffer);
}


WDCConnection::WDCConnection(WebRTCDataChannels* parent, const QString& dataChannelID) :
    _parent(parent),
    _dataChannelID(dataChannelID),
    _peerConnection()
{
#if WEBRTC_DEBUG >= WEBRTC_DEBUG_LEVEL_NORMAL
    qCDebug(networking_webrtc) << "WDCConnection::WDCConnection() :" << dataChannelID;
#endif

    // Create observers.
    _setLocalDescriptionObserver = new rtc::RefCountedObject<WDCSetLocalDescriptionObserver>();
    _setRemoteDescriptionObserver = new rtc::RefCountedObject<WDCSetRemoteDescriptionObserver>();
    _createSessionDescriptionObserver = new rtc::RefCountedObject<WDCCreateSessionDescriptionObserver>(this);
    _dataChannelObserver = std::make_shared<WDCDataChannelObserver>(this);
    _peerConnectionObserver = std::make_shared<WDCPeerConnectionObserver>(this);

    // Create new peer connection.
    _peerConnection = _parent->createPeerConnection(_peerConnectionObserver);
};

void WDCConnection::setRemoteDescription(QJsonObject& description) {
#if WEBRTC_DEBUG >= WEBRTC_DEBUG_LEVEL_VERBOSE
    qCDebug(networking_webrtc) << "WDCConnection::setRemoteDescription() :" << description;
#endif

    SdpParseError sdpParseError;
    auto sdpType = SdpTypeFromString(description.value("type").toString().toStdString());
    if (!sdpType) {
        qCWarning(networking_webrtc) << "Error parsing WebRTC remote description type: "
            << description;
        return;
    }
    auto sessionDescription = CreateSessionDescription(
        *sdpType,
        description.value("sdp").toString().toStdString(),
        &sdpParseError);
    if (!sessionDescription) {
        qCWarning(networking_webrtc) << "Error creating WebRTC remote description:"
            << QString::fromStdString(sdpParseError.description);
        return;
    }

#if WEBRTC_DEBUG >= WEBRTC_DEBUG_LEVEL_VERBOSE
    qCDebug(networking_webrtc) << "3. Set remote description:" << sessionDescription.get();
#endif
    if (_peerConnection) {
// NOTE: this indicates a newer WebRTC build in use on linux,
// TODO: remove the old code when windows WebRTC is updated as well
#ifdef API_SET_LOCAL_DESCRIPTION_OBSERVER_INTERFACE_H_
        _peerConnection->SetRemoteDescription(std::move(sessionDescription), _setRemoteDescriptionObserver);
#else
        _peerConnection->SetRemoteDescription(_setRemoteDescriptionObserver, sessionDescription.release());
#endif
    }
}

void WDCConnection::createAnswer() {
#if WEBRTC_DEBUG >= WEBRTC_DEBUG_LEVEL_VERBOSE
    qCDebug(networking_webrtc) << "WDCConnection::createAnswer()";
    qCDebug(networking_webrtc) << "4.a Create answer";
#endif
    if (_peerConnection) {
        _peerConnection->CreateAnswer(_createSessionDescriptionObserver.get(), PeerConnectionInterface::RTCOfferAnswerOptions());
    }
}

void WDCConnection::sendAnswer(std::string descriptionString) {
#if WEBRTC_DEBUG >= WEBRTC_DEBUG_LEVEL_VERBOSE
    qCDebug(networking_webrtc) << "WDCConnection::sendAnswer()";
    qCDebug(networking_webrtc) << "4.b Send answer to the remote peer";
#endif

    QJsonObject jsonDescription;
    jsonDescription.insert("sdp", QString::fromStdString(descriptionString));
    jsonDescription.insert("type", "answer");

    QJsonObject jsonWebRTCPayload;
    jsonWebRTCPayload.insert("description", jsonDescription);

    QJsonObject jsonObject;
    jsonObject.insert("from", QString(_parent->getNodeType()));
    jsonObject.insert("to", _dataChannelID);
    jsonObject.insert("data", jsonWebRTCPayload);

    _parent->sendSignalingMessage(jsonObject);
}

void WDCConnection::setLocalDescription(std::unique_ptr<SessionDescriptionInterface> description) {
#if WEBRTC_DEBUG >= WEBRTC_DEBUG_LEVEL_VERBOSE
    qCDebug(networking_webrtc) << "WDCConnection::setLocalDescription()";
    qCDebug(networking_webrtc) << "5. Set local description";
#endif
    if (_peerConnection) {
// NOTE: this indicates a newer WebRTC build in use on linux,
// TODO: remove the old code when windows WebRTC is updated as well
#ifdef API_SET_LOCAL_DESCRIPTION_OBSERVER_INTERFACE_H_
        _peerConnection->SetLocalDescription(std::move(description), _setLocalDescriptionObserver);
#else
        _peerConnection->SetLocalDescription(_setLocalDescriptionObserver, description.release());
#endif
    }
}

void WDCConnection::addIceCandidate(QJsonObject& data) {
#if WEBRTC_DEBUG >= WEBRTC_DEBUG_LEVEL_VERBOSE
    qCDebug(networking_webrtc) << "WDCConnection::addIceCandidate()";
#endif

    SdpParseError sdpParseError;
    auto iceCandidate = CreateIceCandidate(
        data.value("sdpMid").toString().toStdString(),
        data.value("sdpMLineIndex").toInt(),
        data.value("candidate").toString().toStdString(),
        &sdpParseError);
    if (!iceCandidate) {
        qCWarning(networking_webrtc) << "Error adding WebRTC ICE candidate:"
            << QString::fromStdString(sdpParseError.description);
        return;
    }

#if WEBRTC_DEBUG >= WEBRTC_DEBUG_LEVEL_VERBOSE
    qCDebug(networking_webrtc) << "6. Add ICE candidate";
#endif
    if (_peerConnection) {
        _peerConnection->AddIceCandidate(iceCandidate);
    }
}

void WDCConnection::sendIceCandidate(const IceCandidateInterface* candidate) {
#if WEBRTC_DEBUG >= WEBRTC_DEBUG_LEVEL_VERBOSE
    qCDebug(networking_webrtc) << "WDCConnection::sendIceCandidate()";
#endif

    std::string candidateString;
    candidate->ToString(&candidateString);
    QJsonObject jsonCandidate;
    jsonCandidate.insert("candidate", QString::fromStdString(candidateString));
    jsonCandidate.insert("sdpMid", QString::fromStdString(candidate->sdp_mid()));
    jsonCandidate.insert("sdpMLineIndex", candidate->sdp_mline_index());

    QJsonObject jsonWebRTCData;
    jsonWebRTCData.insert("candidate", jsonCandidate);

    QJsonObject jsonObject;
    jsonObject.insert("from", QString(_parent->getNodeType()));
    jsonObject.insert("to", _dataChannelID);
    jsonObject.insert("data", jsonWebRTCData);
    QJsonDocument jsonDocument = QJsonDocument(jsonObject);

#if WEBRTC_DEBUG >= WEBRTC_DEBUG_LEVEL_VERBOSE
    qCDebug(networking_webrtc) << "7. Send ICE candidate to the remote peer";
#endif
    _parent->sendSignalingMessage(jsonObject);
}

void WDCConnection::onPeerConnectionStateChanged(PeerConnectionInterface::PeerConnectionState state) {
#if WEBRTC_DEBUG >= WEBRTC_DEBUG_LEVEL_NORMAL
    QStringList states {
        "New",
        "Connecting",
        "Connected",
        "Disconnected",
        "Failed",
        "Closed"
    };
    qCDebug(networking_webrtc) << "WDCConnection::onPeerConnectionStateChanged() :" << (int)state << states[(int)state];
#endif
}

void WDCConnection::onDataChannelOpened(rtc::scoped_refptr<DataChannelInterface> dataChannel) {
#if WEBRTC_DEBUG >= WEBRTC_DEBUG_LEVEL_NORMAL
    qCDebug(networking_webrtc) << "WDCConnection::onDataChannelOpened() :"
        << dataChannel->id()
        << QString::fromStdString(dataChannel->label())
        << QString::fromStdString(dataChannel->protocol())
        << dataChannel->negotiated()
        << dataChannel->maxRetransmitTime()
        << dataChannel->maxRetransmits()
        << dataChannel->maxPacketLifeTime().value_or(-1)
        << dataChannel->maxRetransmitsOpt().value_or(-1);
#endif

    _dataChannel = dataChannel;
    _dataChannel->RegisterObserver(_dataChannelObserver.get());

#if WEBRTC_DEBUG >= WEBRTC_DEBUG_LEVEL_NORMAL
    qCDebug(networking_webrtc) << "WDCConnection::onDataChannelOpened() : channel ID:" << _dataChannelID;
#endif
    _parent->onDataChannelOpened(this, _dataChannelID);
}

void WDCConnection::onDataChannelStateChanged() {
    auto state = _dataChannel->state();
#if WEBRTC_DEBUG >= WEBRTC_DEBUG_LEVEL_NORMAL
    qCDebug(networking_webrtc) << "WDCConnection::onDataChannelStateChanged() :" << (int)state
        << DataChannelInterface::DataStateString(state);
#endif
    if (state == DataChannelInterface::kClosed) {
        // Finish with the data channel.
        _dataChannel->UnregisterObserver();
        // Don't set _dataChannel = nullptr because it is a scoped_refptr.
        _dataChannelObserver = nullptr;

        // Close peer connection.
        _parent->closePeerConnection(this);
    }
}

void WDCConnection::onDataChannelMessageReceived(const DataBuffer& buffer) {
#if WEBRTC_DEBUG >= WEBRTC_DEBUG_LEVEL_VERBOSE
    qCDebug(networking_webrtc) << "WDCConnection::onDataChannelMessageReceived()";
#endif

    auto byteArray = QByteArray(buffer.data.data<char>(), (int)buffer.data.size());

    // Echo message back to sender.
    if (byteArray.startsWith("echo:")) {
#if WEBRTC_DEBUG >= WEBRTC_DEBUG_LEVEL_VERBOSE
    qCDebug(networking_webrtc) << "Echo message back";
#endif
        auto addressParts = _dataChannelID.split(":");
        if (addressParts.length() != 2) {
            qCWarning(networking_webrtc) << "Invalid dataChannelID:" << _dataChannelID;
            return;
        }
        auto address = SockAddr(SocketType::WebRTC, QHostAddress(addressParts[0]), addressParts[1].toInt());
        _parent->sendDataMessage(address, byteArray);  // Use parent method to exercise the code stack.
        return;
    }

    _parent->emitDataMessage(_dataChannelID, byteArray);
}

qint64 WDCConnection::getBufferedAmount() const {
#if WEBRTC_DEBUG >= WEBRTC_DEBUG_LEVEL_VERBOSE
    qCDebug(networking_webrtc) << "WDCConnection::getBufferedAmount()";
#endif
    return _dataChannel && _dataChannel->state() != DataChannelInterface::kClosing
            && _dataChannel->state() != DataChannelInterface::kClosed
        ? _dataChannel->buffered_amount() : 0;
}

bool WDCConnection::sendDataMessage(const DataBuffer& buffer) {
#if WEBRTC_DEBUG >= WEBRTC_DEBUG_LEVEL_VERBOSE
    qCDebug(networking_webrtc) << "WDCConnection::sendDataMessage()";
    if (!_dataChannel || _dataChannel->state() == DataChannelInterface::kClosing
        || _dataChannel->state() == DataChannelInterface::kClosed) {
        qCDebug(networking_webrtc) << "No data channel to send on";
    }
#endif
    if (!_dataChannel || _dataChannel->state() == DataChannelInterface::kClosing
            || _dataChannel->state() == DataChannelInterface::kClosed) {
        // Data channel may have been closed while message to send was being prepared.
        return false;
    } else if (_dataChannel->buffered_amount() + buffer.size() > MAX_WEBRTC_BUFFER_SIZE) {
        // Don't send, otherwise the data channel will be closed.
        qCDebug(networking_webrtc) << "WebRTC send buffer overflow";
        return false;
    }
    return _dataChannel->Send(buffer);
}

void WDCConnection::closePeerConnection() {
#if WEBRTC_DEBUG >= WEBRTC_DEBUG_LEVEL_NORMAL
    if (_peerConnection) {
        qCDebug(networking_webrtc) << "WDCConnection::closePeerConnection() :" << (int)_peerConnection->peer_connection_state();
    }
#endif
    if (_peerConnection) {
        _peerConnection->Close();
    }
    // Don't set _peerConnection = nullptr because it is a scoped_refptr.
    _peerConnectionObserver = nullptr;
#if WEBRTC_DEBUG >= WEBRTC_DEBUG_LEVEL_NORMAL
    qCDebug(networking_webrtc) << "Disposed of peer connection";
#endif
}


WebRTCDataChannels::WebRTCDataChannels(QObject* parent) :
    QObject(parent),
    _parent(parent)
{
#if WEBRTC_DEBUG >= WEBRTC_DEBUG_LEVEL_NORMAL
    qCDebug(networking_webrtc) << "WebRTCDataChannels::WebRTCDataChannels()";
#endif

    // Create a peer connection factory.
#if WEBRTC_DEBUG >= WEBRTC_DEBUG_LEVEL_NORMAL
    // Numbers are per WebRTC's peer_connection_interface.h.
    qCDebug(networking_webrtc) << "1. Create a new PeerConnectionFactoryInterface";
#endif
    _rtcNetworkThread = rtc::Thread::CreateWithSocketServer();
    _rtcNetworkThread->Start();
    _rtcWorkerThread = rtc::Thread::Create();
    _rtcWorkerThread->Start();
    _rtcSignalingThread = rtc::Thread::Create();
    _rtcSignalingThread->Start();
    PeerConnectionFactoryDependencies dependencies;
    dependencies.network_thread = _rtcNetworkThread.get();
    dependencies.worker_thread = _rtcWorkerThread.get();
    dependencies.signaling_thread = _rtcSignalingThread.get();
    _peerConnectionFactory = CreateModularPeerConnectionFactory(std::move(dependencies));
    if (!_peerConnectionFactory) {
        qCWarning(networking_webrtc) << "Failed to create WebRTC peer connection factory";
    }

    // Set up mechanism for closing peer connections.
    connect(this, &WebRTCDataChannels::closePeerConnectionSoon, this, &WebRTCDataChannels::closePeerConnectionNow);
}

WebRTCDataChannels::~WebRTCDataChannels() {
#if WEBRTC_DEBUG >= WEBRTC_DEBUG_LEVEL_NORMAL
    qCDebug(networking_webrtc) << "WebRTCDataChannels::~WebRTCDataChannels()";
#endif
    reset();
    _peerConnectionFactory = nullptr;
    _rtcSignalingThread->Stop();
    _rtcSignalingThread = nullptr;
    _rtcWorkerThread->Stop();
    _rtcWorkerThread = nullptr;
    _rtcNetworkThread->Stop();
    _rtcNetworkThread = nullptr;
}

void WebRTCDataChannels::reset() {
#if WEBRTC_DEBUG >= WEBRTC_DEBUG_LEVEL_NORMAL
    qCDebug(networking_webrtc) << "WebRTCDataChannels::reset() :" << _connectionsByID.count();
#endif
    QHashIterator<QString, WDCConnection*> i(_connectionsByID);
    while (i.hasNext()) {
        i.next();
        delete i.value();
    }
    _connectionsByID.clear();
}

void WebRTCDataChannels::onDataChannelOpened(WDCConnection* connection, const QString& dataChannelID) {
#if WEBRTC_DEBUG >= WEBRTC_DEBUG_LEVEL_NORMAL
    qCDebug(networking_webrtc) << "WebRTCDataChannels::onDataChannelOpened() :" << dataChannelID;
#endif
    _connectionsByID.insert(dataChannelID, connection);
}

void WebRTCDataChannels::onSignalingMessage(const QJsonObject& message) {
#if WEBRTC_DEBUG >= WEBRTC_DEBUG_LEVEL_NORMAL
    qCDebug(networking_webrtc) << "WebRTCDataChannel::onSignalingMessage()" << message;
#endif

    // Validate message.
    const int MAX_DEBUG_DETAIL_LENGTH = 64;
    const QRegularExpression DATA_CHANNEL_ID_REGEX{ "^[1-9]\\d*\\.\\d+\\.\\d+\\.\\d+:\\d+$" };
    auto data = message.value("data").isObject() ? message.value("data").toObject() : QJsonObject();
    auto from = message.value("from").toString();
    auto to = NodeType::fromChar(message.value("to").toString().at(0));
    if (!DATA_CHANNEL_ID_REGEX.match(from).hasMatch() || to == NodeType::Unassigned
            || !data.contains("description") && !data.contains("candidate")) {
        qCWarning(networking_webrtc) << "Invalid or unexpected signaling message:"
            << QJsonDocument(message).toJson(QJsonDocument::Compact).left(MAX_DEBUG_DETAIL_LENGTH);
        return;
    }

    // Remember this node's type for the reply.
    _nodeType = to;

    // Find or create a connection.
    WDCConnection* connection;
    if (_connectionsByID.contains(from)) {
        connection = _connectionsByID.value(from);
    } else {
        connection = new WDCConnection(this, from);
        _connectionsByID.insert(from, connection);
    }

    // Set the remote description and reply with an answer.
    if (data.contains("description")) {
        auto description = data.value("description").toObject();
        if (description.value("type").toString() == "offer") {
            connection->setRemoteDescription(description);
            connection->createAnswer();
        } else {
            qCWarning(networking_webrtc) << "Unexpected signaling description:"
                << QJsonDocument(description).toJson(QJsonDocument::Compact).left(MAX_DEBUG_DETAIL_LENGTH);
        }
    }

    // Add a remote ICE candidate.
    if (data.contains("candidate")) {
        auto candidate = data.value("candidate").toObject();
        connection->addIceCandidate(candidate);
    }

}

void WebRTCDataChannels::sendSignalingMessage(const QJsonObject& message) {
#if WEBRTC_DEBUG >= WEBRTC_DEBUG_LEVEL_NORMAL
    qCDebug(networking_webrtc) << "WebRTCDataChannels::sendSignalingMessage() :" << QJsonDocument(message).toJson(QJsonDocument::Compact);
#endif
    emit signalingMessage(message);
}

void WebRTCDataChannels::emitDataMessage(const QString& dataChannelID, const QByteArray& byteArray) {
#if WEBRTC_DEBUG >= WEBRTC_DEBUG_LEVEL_VERBOSE
    qCDebug(networking_webrtc) << "WebRTCDataChannels::emitDataMessage() :" << dataChannelID << byteArray.toHex()
        << byteArray.length();
#endif
    auto addressParts = dataChannelID.split(":");
    if (addressParts.length() != 2) {
        qCWarning(networking_webrtc) << "Invalid dataChannelID:" << dataChannelID;
        return;
    }
    auto address = SockAddr(SocketType::WebRTC, QHostAddress(addressParts[0]), addressParts[1].toInt());
    emit dataMessage(address, byteArray);
}

bool WebRTCDataChannels::sendDataMessage(const SockAddr& destination, const QByteArray& byteArray) {
    auto dataChannelID = destination.toShortString();
#if WEBRTC_DEBUG >= WEBRTC_DEBUG_LEVEL_VERBOSE
    qCDebug(networking_webrtc) << "WebRTCDataChannels::sendDataMessage() :" << dataChannelID;
#endif

    if (!_connectionsByID.contains(dataChannelID)) {
        qCWarning(networking_webrtc) << "Could not find WebRTC data channel to send message on!";
        return false;
    }

    auto connection = _connectionsByID.value(dataChannelID);
    DataBuffer buffer(rtc::CopyOnWriteBuffer(byteArray.data(), byteArray.size()), true);
    return connection->sendDataMessage(buffer);
}

qint64 WebRTCDataChannels::getBufferedAmount(const SockAddr& address) const {
    auto dataChannelID = address.toShortString();
    if (!_connectionsByID.contains(dataChannelID)) {
#if WEBRTC_DEBUG >= WEBRTC_DEBUG_LEVEL_VERBOSE
        qCDebug(networking_webrtc) << "WebRTCDataChannels::getBufferedAmount() : Channel doesn't exist:" << dataChannelID;
#endif
        return 0;
    }
    auto connection = _connectionsByID.value(dataChannelID);
    return connection->getBufferedAmount();
}

rtc::scoped_refptr<PeerConnectionInterface> WebRTCDataChannels::createPeerConnection(
        const std::shared_ptr<WDCPeerConnectionObserver> peerConnectionObserver) {
#if WEBRTC_DEBUG >= WEBRTC_DEBUG_LEVEL_NORMAL
    qCDebug(networking_webrtc) << "WebRTCDataChannels::createPeerConnection()";
#endif

    PeerConnectionInterface::RTCConfiguration configuration;

    const QString WEBRTC_ICE_SERVERS_OVERRIDE_ENV = "VRCA_OVERRIDE_WEBRTC_ICE_SERVERS";
    if (QProcessEnvironment::systemEnvironment().contains(WEBRTC_ICE_SERVERS_OVERRIDE_ENV)) {
        auto envString = QProcessEnvironment::systemEnvironment().value(WEBRTC_ICE_SERVERS_OVERRIDE_ENV).toUtf8();
        auto json = QJsonDocument::fromJson(envString);
        if (json.isArray()) {
            auto array = json.array();
            for (auto&& server : array) {
                PeerConnectionInterface::IceServer iceServer;
                if (server.isObject() && server.toObject().contains("urls")) {
                    auto urls = server.toObject()["urls"];
                    if (urls.isArray()) {
                        for (auto&& url : urls.toArray()) {
                            iceServer.urls.push_back(url.toString().toStdString());
                        }
                    } else {
                        iceServer.urls.push_back(urls.toString().toStdString());
                    }
                    auto password = server.toObject()["credential"].toString().toStdString();
                    auto username = server.toObject()["username"].toString().toStdString();
                    if (password != "") {
                        iceServer.password = password;
                    }
                    if (username != "") {
                        iceServer.username = username;
                    }
                    configuration.servers.push_back(iceServer);
                } else {
                    qCDebug(networking_webrtc) << "WebRTCDataChannels::createPeerConnection() : " << WEBRTC_ICE_SERVERS_OVERRIDE_ENV << " environment variable invalid";
                    qCDebug(networking_webrtc) << envString;
                    break;
                }
            }
        } else {
            qCDebug(networking_webrtc) << "WebRTCDataChannels::createPeerConnection() : " << WEBRTC_ICE_SERVERS_OVERRIDE_ENV << " environment variable is not a JSON array";
            qCDebug(networking_webrtc) << envString;
        }
    } else {
        configuration.servers = _iceServers;
        if (configuration.servers.empty()) {
            for (const auto& url : DEFAULT_ICE_SERVER_URLS) {
                PeerConnectionInterface::IceServer iceServer;
                iceServer.urls = std::vector<std::string>{url};
                configuration.servers.push_back(iceServer);
            }
        }
    }

#if WEBRTC_DEBUG >= WEBRTC_DEBUG_LEVEL_NORMAL
    qCDebug(networking_webrtc) << "WebRTCDataChannels::createPeerConnection() : Configuration ICE server list:";
        for (const auto& server : configuration.servers) {
            qCDebug(networking_webrtc) << "URL: " << (server.urls.size() > 0 ? server.urls.front().c_str() : "");
            if (server.username != "") {
                qCDebug(networking_webrtc) << "USERNAME: " << server.username.c_str();
            }
            if (server.password != "") {
                qCDebug(networking_webrtc) << "PASSWORD: " << server.password.c_str();
            }
        }
#endif

#if WEBRTC_DEBUG >= WEBRTC_DEBUG_LEVEL_NORMAL
    qCDebug(networking_webrtc) << "2. Create a new peer connection";
#endif
    PeerConnectionDependencies dependencies(peerConnectionObserver.get());

// NOTE: this indicates a newer WebRTC build in use on linux,
// TODO: remove the old code when windows WebRTC is updated as well
#ifdef API_SET_LOCAL_DESCRIPTION_OBSERVER_INTERFACE_H_
    auto result = _peerConnectionFactory->CreatePeerConnectionOrError(configuration, std::move(dependencies));
    if (result.ok()) {
#if WEBRTC_DEBUG >= WEBRTC_DEBUG_LEVEL_NORMAL
        qCDebug(networking_webrtc) << "Created peer connection";
#endif
        return result.MoveValue();
    } else {
        qCCritical(networking_webrtc) << "WebRTCDataChannels::createPeerConnection(): Failed: " << result.error().message();
        return nullptr;
    }
#else
    auto result = _peerConnectionFactory->CreatePeerConnection(configuration, std::move(dependencies));
#if WEBRTC_DEBUG >= WEBRTC_DEBUG_LEVEL_NORMAL
    qCDebug(networking_webrtc) << "Created peer connection";
#endif
    return result;
#endif

}


void WebRTCDataChannels::closePeerConnection(WDCConnection* connection) {
#if WEBRTC_DEBUG >= WEBRTC_DEBUG_LEVEL_NORMAL
    qCDebug(networking_webrtc) << "WebRTCDataChannels::closePeerConnection()";
#endif
    // Use Qt's signals/slots mechanism to close the peer connection on its own call stack, separate from the DataChannel
    // callback that initiated the peer connection.
    // https://bugs.chromium.org/p/webrtc/issues/detail?id=3721
    emit closePeerConnectionSoon(connection);
}


void WebRTCDataChannels::closePeerConnectionNow(WDCConnection* connection) {
#if WEBRTC_DEBUG >= WEBRTC_DEBUG_LEVEL_NORMAL
    qCDebug(networking_webrtc) << "WebRTCDataChannels::closePeerConnectionNow()";
#endif
    // Close the peer connection.
    connection->closePeerConnection();

    // Delete the WDCConnection.
#if WEBRTC_DEBUG >= WEBRTC_DEBUG_LEVEL_NORMAL
    qCDebug(networking_webrtc) << "Dispose of connection for channel:" << connection->getDataChannelID();
#endif
    _connectionsByID.remove(connection->getDataChannelID());
    delete connection;
#if WEBRTC_DEBUG >= WEBRTC_DEBUG_LEVEL_NORMAL
    qCDebug(networking_webrtc) << "Disposed of connection";
#endif
}

void WebRTCDataChannels::setIceServers(std::vector<PeerConnectionInterface::IceServer> iceServers) {
    _iceServers = std::move(iceServers);
}

#endif // WEBRTC_DATA_CHANNELS
