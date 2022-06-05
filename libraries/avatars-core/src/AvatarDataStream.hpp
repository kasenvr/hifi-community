//
//  AvatarDataStream.hpp
//  libraries/avatars-core/src
//
//  Created by Nshan G. on 21 Apr 2022.
//  Copyright 2013 High Fidelity, Inc.
//  Copyright 2022 Vircadia contributors.
//  Copyright 2022 DigiSomni LLC.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#ifndef LIBRARIES_AVATARS_CORE_SRC_AVATARDATASTREAM_HPP
#define LIBRARIES_AVATARS_CORE_SRC_AVATARDATASTREAM_HPP

#include "AvatarDataStream.h"

#include <cstdio>
#include <cstring>
#include <algorithm>
#include <functional>
#include <stdint.h>

#include <QtCore/QDataStream>
#include <QtCore/QThread>
#include <QtCore/QUuid>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>

#include <shared/QtHelpers.h>
#include <QVariantGLM.h>
#include <NetworkAccessManager.h>
#include <NodeList.h>
#include <udt/PacketHeaders.h>
#include <GLMHelpers.h>
#include <StreamUtils.h>
#include <UUID.h>
#include <ShapeInfo.h>
#include <AudioHelpers.h>
#include <Profile.h>
#include <VariantMapToScriptValue.h>
#include <BitVectorHelpers.h>
#include <NumericalConstants.h>

#include "AvatarLogging.h"
#include "AvatarTraits.h"
#include "ClientTraitsHandler.hpp"
#include "ResourceRequestObserver.h"

//#define WANT_DEBUG

using namespace std;

inline const int TRANSLATION_COMPRESSION_RADIX = 14;
inline const int HAND_CONTROLLER_COMPRESSION_RADIX = 12;
inline const int SENSOR_TO_WORLD_SCALE_RADIX = 10;
inline const float AUDIO_LOUDNESS_SCALE = 1024.0f;
inline const float DEFAULT_AVATAR_DENSITY = 1000.0f; // density of water

#define ASSERT(COND)  do { if (!(COND)) { abort(); } } while(0)

template <typename Derived>
AvatarDataStream<Derived>::AvatarDataStream() {}

template <typename Derived>
AvatarDataStream<Derived>::~AvatarDataStream() {}

template <typename Derived>
Derived& AvatarDataStream<Derived>::derived() {
    return *static_cast<Derived*>(this);
}

template <typename Derived>
const Derived& AvatarDataStream<Derived>::derived() const {
    return *static_cast<const Derived*>(this);
}

template <typename Derived>
float AvatarDataStream<Derived>::getDistanceBasedMinRotationDOT(glm::vec3 viewerPosition) const {
    auto pos = derived().getGlobalPositionOut();
    auto distance = glm::distance(glm::vec3(pos.globalPosition.data[0], pos.globalPosition.data[1], pos.globalPosition.data[2]), viewerPosition);
    float result = ROTATION_CHANGE_179D; // assume worst
    if (distance < AVATAR_DISTANCE_LEVEL_1) {
        result = AVATAR_MIN_ROTATION_DOT;
    } else if (distance < AVATAR_DISTANCE_LEVEL_2) {
        result = ROTATION_CHANGE_2D;
    } else if (distance < AVATAR_DISTANCE_LEVEL_3) {
        result = ROTATION_CHANGE_4D;
    } else if (distance < AVATAR_DISTANCE_LEVEL_4) {
        result = ROTATION_CHANGE_6D;
    } else if (distance < AVATAR_DISTANCE_LEVEL_5) {
        result = ROTATION_CHANGE_15D;
    }
    return result;
}

template <typename Derived>
QUuid AvatarDataStream<Derived>::grab(const QUuid& targetID, int parentJointIndex,
                           glm::vec3 positionalOffset, glm::quat rotationalOffset) {
    auto grabID = QUuid::createUuid();
    // create a temporary grab object to get grabData

    QString hand = "none";
    if (parentJointIndex == CONTROLLER_RIGHTHAND_INDEX ||
        parentJointIndex == CAMERA_RELATIVE_CONTROLLER_RIGHTHAND_INDEX ||
        parentJointIndex == FARGRAB_RIGHTHAND_INDEX ||
        parentJointIndex == derived().getJointIndex("RightHand")) {
        hand = "right";
    } else if (parentJointIndex == CONTROLLER_LEFTHAND_INDEX ||
               parentJointIndex == CAMERA_RELATIVE_CONTROLLER_LEFTHAND_INDEX ||
               parentJointIndex == FARGRAB_LEFTHAND_INDEX ||
               parentJointIndex == derived().getJointIndex("LeftHand")) {
        hand = "left";
    }

    Grab tmpGrab(derived().getSessionUUIDOut(),
                 targetID, parentJointIndex, hand, positionalOffset, rotationalOffset);
    QByteArray grabData = tmpGrab.toByteArray();
    bool dataChanged = updateAvatarGrabData(grabID, grabData);

    if (dataChanged && derived().getClientTraitsHandler()) {
        // indicate that the changed data should be sent to the mixer
        derived().getClientTraitsHandler()->markInstancedTraitUpdated(AvatarTraits::Grab, grabID);
    }

    return grabID;
}

template <typename Derived>
void AvatarDataStream<Derived>::releaseGrab(const QUuid& grabID) {
    bool tellHandler { false };

    _avatarGrabsLock.withWriteLock([&] {

        // FIXME: CRTP
        // std::map<QUuid, GrabPointer>::iterator itr;
        // itr = _avatarGrabs.find(grabID);
        // if (itr != _avatarGrabs.end()) {
        //     GrabPointer grab = itr->second;
        //     if (grab) {
        //         grab->setReleased(true);
        //         bool success;
        //         SpatiallyNestablePointer target = SpatiallyNestable::findByID(grab->getTargetID(), success);
        //         if (target && success) {
        //             target->disableGrab(grab);
        //         }
        //     }
        // }

        if (_avatarGrabData.remove(grabID)) {
            derived().onGrabRemoved(grabID);
            // FIXE: CRTP
            // _grabsToDelete.push_back(grabID);
            tellHandler = true;
        }
    });

    if (tellHandler && derived().getClientTraitsHandler()) {
        // indicate the deletion of the data to the mixer
        derived().getClientTraitsHandler()->markInstancedTraitDeleted(AvatarTraits::Grab, grabID);
    }
}

template <typename Derived>
float AvatarDataStream<Derived>::getDistanceBasedMinTranslationDistance(glm::vec3 viewerPosition) const {
    return AVATAR_MIN_TRANSLATION; // Eventually make this distance sensitive as well
}


// FIXME: CRTP move to derived
// we want to track outbound data in this case...
// QByteArray AvatarData::toByteArrayStateful(AvatarDataDetail dataDetail, bool dropFaceTracking) {
//     auto lastSentTime = _lastToByteArray;
//     _lastToByteArray = usecTimestampNow();
//     AvatarDataPacket::SendStatus sendStatus;
//     auto avatarByteArray = AvatarData::toByteArray(dataDetail, lastSentTime, getLastSentJointData(),
//         sendStatus, dropFaceTracking, false, glm::vec3(0), nullptr, 0, &_outboundDataRate);
//     return avatarByteArray;
// }

// FIXME: CRTP move to derived
// QByteArray AvatarData::toByteArray(AvatarDataDetail dataDetail, quint64 lastSentTime,
//                                    const QVector<JointData>& lastSentJointData, AvatarDataPacket::SendStatus& sendStatus,
//                                    bool dropFaceTracking, bool distanceAdjust, glm::vec3 viewerPosition,
//                                    QVector<JointData>* sentJointDataOut,
//                                    int maxDataSize, AvatarDataRate* outboundDataRateOut) const {
//     AvatarDataPacket::HasFlags itemFlagsInit = 0;
//
//     lazyInitHeadData();
//     if(sendStatus.itemFlags == 0) { // New avatar ...
//         bool hasAvatarGlobalPosition = true; // always include global position
//         bool hasAvatarOrientation = false;
//         bool hasAvatarBoundingBox = false;
//         bool hasAvatarScale = false;
//         bool hasLookAtPosition = false;
//         bool hasAudioLoudness = false;
//         bool hasSensorToWorldMatrix = false;
//         bool hasJointData = false;
//         bool hasJointDefaultPoseFlags = false;
//         bool hasAdditionalFlags = false;
//
//         // local position, and parent info only apply to avatars that are parented. The local position
//         // and the parent info can change independently though, so we track their "changed since"
//         // separately
//         bool hasParentInfo = false;
//         bool hasAvatarLocalPosition = false;
//         bool hasHandControllers = false;
//
//         bool hasFaceTrackerInfo = false;
//
//         if (dataDetail == PALMinimum) {
//             hasAudioLoudness = true;
//         } else {
//             bool sendAll = (dataDetail == SendAllData);
//             hasAvatarOrientation = sendAll || rotationChangedSince(lastSentTime);
//             hasAvatarBoundingBox = sendAll || avatarBoundingBoxChangedSince(lastSentTime);
//             hasAvatarScale = sendAll || avatarScaleChangedSince(lastSentTime);
//             hasLookAtPosition = sendAll || lookAtPositionChangedSince(lastSentTime);
//             hasAudioLoudness = sendAll || audioLoudnessChangedSince(lastSentTime);
//             hasSensorToWorldMatrix = sendAll || sensorToWorldMatrixChangedSince(lastSentTime);
//             hasAdditionalFlags = sendAll || additionalFlagsChangedSince(lastSentTime);
//             hasParentInfo = sendAll || parentInfoChangedSince(lastSentTime);
//             hasAvatarLocalPosition = hasParent() && (sendAll ||
//                 tranlationChangedSince(lastSentTime) ||
//                 parentInfoChangedSince(lastSentTime));
//             hasHandControllers = _controllerLeftHandMatrixCache.isValid() || _controllerRightHandMatrixCache.isValid();
//             hasFaceTrackerInfo = !dropFaceTracking && (getHasScriptedBlendshapes() || _headData->_hasInputDrivenBlendshapes) &&
//                 (sendAll || faceTrackerInfoChangedSince(lastSentTime));
//             hasJointData = !(dataDetail == MinimumData);
            // hasJointDefaultPoseFlags = hasJointData;
//         }
//
//         bool leftValid = _farGrabLeftMatrixCache.isValid();
//         bool rightValid = _farGrabRightMatrixCache.isValid();
//         bool mouseValid = _farGrabMouseMatrixCache.isValid();
//
//         itemFlagsInit =
//             (hasAvatarGlobalPosition ? AvatarDataPacket::PACKET_HAS_AVATAR_GLOBAL_POSITION : 0)
//             | (hasAvatarBoundingBox ? AvatarDataPacket::PACKET_HAS_AVATAR_BOUNDING_BOX : 0)
//             | (hasAvatarOrientation ? AvatarDataPacket::PACKET_HAS_AVATAR_ORIENTATION : 0)
//             | (hasAvatarScale ? AvatarDataPacket::PACKET_HAS_AVATAR_SCALE : 0)
//             | (hasLookAtPosition ? AvatarDataPacket::PACKET_HAS_LOOK_AT_POSITION : 0)
//             | (hasAudioLoudness ? AvatarDataPacket::PACKET_HAS_AUDIO_LOUDNESS : 0)
//             | (hasSensorToWorldMatrix ? AvatarDataPacket::PACKET_HAS_SENSOR_TO_WORLD_MATRIX : 0)
//             | (hasAdditionalFlags ? AvatarDataPacket::PACKET_HAS_ADDITIONAL_FLAGS : 0)
//             | (hasParentInfo ? AvatarDataPacket::PACKET_HAS_PARENT_INFO : 0)
//             | (hasAvatarLocalPosition ? AvatarDataPacket::PACKET_HAS_AVATAR_LOCAL_POSITION : 0)
//             | (hasHandControllers ? AvatarDataPacket::PACKET_HAS_HAND_CONTROLLERS : 0)
//             | (hasFaceTrackerInfo ? AvatarDataPacket::PACKET_HAS_FACE_TRACKER_INFO : 0)
//             | (hasJointData ? AvatarDataPacket::PACKET_HAS_JOINT_DATA : 0)
//             | (hasJointDefaultPoseFlags ? AvatarDataPacket::PACKET_HAS_JOINT_DEFAULT_POSE_FLAGS : 0)
//             | (hasJointData && (leftValid || rightValid || mouseValid) ? AvatarDataPacket::PACKET_HAS_GRAB_JOINTS : 0);
//     }
//
//     return toByteArray(itemFlagsInit, dataDetail, lastSentJointData, sendStatus, distanceAdjust, viewerPosition,
//             sentJointDataOut, maxDataSize, outboundDataRateOut);
// }

template <typename Derived>
QByteArray AvatarDataStream<Derived>::toByteArray(AvatarDataPacket::HasFlags itemFlagsInit, AvatarDataDetail dataDetail,
                                   const QVector<JointData>& lastSentJointData, AvatarDataPacket::SendStatus& sendStatus,
                                   bool distanceAdjust, glm::vec3 viewerPosition,
                                   QVector<JointData>* sentJointDataOut,
                                   int maxDataSize, AvatarDataRate* outboundDataRateOut) const {

    ASSERT(maxDataSize == 0 || (size_t)maxDataSize >= AvatarDataPacket::MIN_BULK_PACKET_SIZE);

    // special case, if we were asked for no data, then just include the flags all set to nothing
    if (dataDetail == NoData) {
        sendStatus.itemFlags = 0;

        QByteArray avatarDataByteArray;
        if (sendStatus.sendUUID) {
            auto convertedId = derived().getSessionUUIDOut().toRfc4122();
            avatarDataByteArray.append(convertedId.data(), NUM_BYTES_RFC4122_UUID);
        }

        avatarDataByteArray.append((char*) &sendStatus.itemFlags, sizeof sendStatus.itemFlags);
        return avatarDataByteArray;
    }

    bool cullSmallChanges = (dataDetail == CullSmallData);
    bool sendAll = (dataDetail == SendAllData);

    // Leading flags, to indicate how much data is actually included in the packet...
    AvatarDataPacket::HasFlags wantedFlags = 0;
    AvatarDataPacket::HasFlags includedFlags = 0;
    AvatarDataPacket::HasFlags extraReturnedFlags = 0;  // For partial joint data.


    // FIXME -
    //
    //    BUG -- if you enter a space bubble, and then back away, the avatar has wrong orientation until "send all" happens...
    //      this is an iFrame issue... what to do about that?
    //
    //    BUG -- Resizing avatar seems to "take too long"... the avatar doesn't redraw at smaller size right away
    //
    // TODO consider these additional optimizations in the future
    // 1) SensorToWorld - should we only send this for avatars with attachments?? - 20 bytes - 7.20 kbps
    // 2) GUIID for the session change to 2byte index                   (savings) - 14 bytes - 5.04 kbps
    // 3) Improve Joints -- currently we use rotational tolerances, but if we had skeleton/bone length data
    //    we could do a better job of determining if the change in joints actually translates to visible
    //    changes at distance.
    //
    //    Potential savings:
    //              63 rotations   * 6 bytes = 136kbps
    //              3 translations * 6 bytes = 6.48kbps
    //

    QUuid parentID;

    if (sendStatus.itemFlags == 0) { // New avatar ...
            wantedFlags = itemFlagsInit;
            sendStatus.itemFlags = wantedFlags;
            sendStatus.rotationsSent = 0;
            sendStatus.translationsSent = 0;
    } else {  // Continuing avatar ...
        wantedFlags = sendStatus.itemFlags;
        if (wantedFlags & AvatarDataPacket::PACKET_HAS_GRAB_JOINTS) {
            // Must send joints for grab joints -
            wantedFlags |= AvatarDataPacket::PACKET_HAS_JOINT_DATA;
        }
    }

    auto [parentUUID, parentJointIndex] = derived().getParentInfoOut();
    // FIXME: CRTP
    // parentUUID = parentID.toRfc4122();
    // parentJointIndex = getParentJointIndex();

    auto [
        leftEyeBlink,
        rightEyeBlink,
        averageLoudness,
        browAudioLift,
        headDataBlendhapeCoefficientsSize,
        headDataBlendhapeCoefficients
    ] = derived().getFaceTrackerInfoOut();

    // FIXME: CRTP
    // leftEyeBlink = _headData->_leftEyeBlink;
    // rightEyeBlink = _headData->_rightEyeBlink;
    // averageLoudness = _headData->_averageLoudness;
    // browAudioLift = _headData->_browAudioLift;
    // headDataBlendhapeCoefficientsSize = _headData->getBlendshapeCoefficients().size();
    // headDataBlendhapeCoefficients = _headData->getBlendshapeCoefficients().data();

    const int jointDataSize = derived().getJointDataSizeOut();

    const size_t byteArraySize = AvatarDataPacket::MAX_CONSTANT_HEADER_SIZE + NUM_BYTES_RFC4122_UUID +
        AvatarDataPacket::maxFaceTrackerInfoSize(headDataBlendhapeCoefficientsSize) +
        AvatarDataPacket::maxJointDataSize(jointDataSize) +
        AvatarDataPacket::maxJointDefaultPoseFlagsSize(jointDataSize) +
        AvatarDataPacket::FAR_GRAB_JOINTS_SIZE;

    if (maxDataSize == 0) {
        maxDataSize = (int)byteArraySize;
    }

    QByteArray avatarDataByteArray((int)byteArraySize, 0);
    unsigned char* destinationBuffer = reinterpret_cast<unsigned char*>(avatarDataByteArray.data());
    const unsigned char* const startPosition = destinationBuffer;
    const unsigned char* const packetEnd = destinationBuffer + maxDataSize;

#define AVATAR_MEMCPY(src)                          \
    memcpy(destinationBuffer, &(src), sizeof(src)); \
    destinationBuffer += sizeof(src);

// If we want an item and there's sufficient space:
#define IF_AVATAR_SPACE(flag, space)                              \
    if ((wantedFlags & AvatarDataPacket::flag)                    \
        && (packetEnd - destinationBuffer) >= (ptrdiff_t)(space)  \
        && (includedFlags |= AvatarDataPacket::flag))

    if (sendStatus.sendUUID) {
        auto convertedId = derived().getSessionUUIDOut().toRfc4122();
        memcpy(destinationBuffer, convertedId.data(), NUM_BYTES_RFC4122_UUID);
        destinationBuffer += NUM_BYTES_RFC4122_UUID;
    }

    unsigned char * packetFlagsLocation = destinationBuffer;
    destinationBuffer += sizeof(wantedFlags);

    IF_AVATAR_SPACE(PACKET_HAS_AVATAR_GLOBAL_POSITION, sizeof( AvatarDataPacket::AvatarGlobalPosition)) {
        auto startSection = destinationBuffer;
        AvatarDataPacket::AvatarGlobalPosition globalPosition = derived().getGlobalPositionOut();
        AVATAR_MEMCPY(globalPosition);

        int numBytes = destinationBuffer - startSection;

        if (outboundDataRateOut) {
            outboundDataRateOut->globalPositionRate.increment(numBytes);
        }
    }

    IF_AVATAR_SPACE(PACKET_HAS_AVATAR_BOUNDING_BOX, sizeof(AvatarDataPacket::AvatarBoundingBox)) {
        auto startSection = destinationBuffer;
        AvatarDataPacket::AvatarBoundingBox bounds = derived().getBoundingBoxOut();

        // FIXME: CRTP
        // AVATAR_MEMCPY(_globalBoundingBoxDimensions);
        // AVATAR_MEMCPY(_globalBoundingBoxOffset);
        AVATAR_MEMCPY(bounds);

        int numBytes = destinationBuffer - startSection;
        if (outboundDataRateOut) {
            outboundDataRateOut->avatarBoundingBoxRate.increment(numBytes);
        }
    }

    IF_AVATAR_SPACE(PACKET_HAS_AVATAR_ORIENTATION, sizeof(AvatarDataPacket::SixByteQuat)) {
        auto startSection = destinationBuffer;
        glm::quat localOrientation {};

        destinationBuffer += packOrientationQuatToSixBytes(destinationBuffer, derived().getOrientationOut());
        // FIXME: CRTP Derived::getOrientationOut should
        // return getOrientationOutbound();
        // which is virtual

        int numBytes = destinationBuffer - startSection;
        if (outboundDataRateOut) {
            outboundDataRateOut->avatarOrientationRate.increment(numBytes);
        }
    }

    IF_AVATAR_SPACE(PACKET_HAS_AVATAR_SCALE, sizeof(AvatarDataPacket::AvatarScale)) {
        auto startSection = destinationBuffer;
        auto data = reinterpret_cast<AvatarDataPacket::AvatarScale*>(destinationBuffer);
        float scale = derived().getScaleOut();
        // FIXME: CRTP
        // scale = getDomainLimitedScale();
        packFloatRatioToTwoByte((uint8_t*)(&data->scale), scale);
        destinationBuffer += sizeof(AvatarDataPacket::AvatarScale);

        int numBytes = destinationBuffer - startSection;
        if (outboundDataRateOut) {
            outboundDataRateOut->avatarScaleRate.increment(numBytes);
        }
    }


    IF_AVATAR_SPACE(PACKET_HAS_LOOK_AT_POSITION, sizeof(AvatarDataPacket::LookAtPosition) ) {
        AvatarDataPacket::LookAtPosition lookAtPosition = derived().getLookAtPositionOut();

        // TODO: CRTP
        // lookAtPosition.LookAtPosition[0] = _headData->getLookAtPosition().x;
        // lookAtPosition.LookAtPosition[0] = _headData->getLookAtPosition().y;
        // lookAtPosition.LookAtPosition[0] = _headData->getLookAtPosition().z;

        auto startSection = destinationBuffer;
        AVATAR_MEMCPY(lookAtPosition);
        int numBytes = destinationBuffer - startSection;
        if (outboundDataRateOut) {
            outboundDataRateOut->lookAtPositionRate.increment(numBytes);
        }
    }

    IF_AVATAR_SPACE(PACKET_HAS_AUDIO_LOUDNESS, sizeof(AvatarDataPacket::AudioLoudness)) {
        auto startSection = destinationBuffer;
        auto data = reinterpret_cast<AvatarDataPacket::AudioLoudness*>(destinationBuffer);
        // FIXME: CRTP return gerAudioLoudness from Derived::getAudioLoudnessOut
        data->audioLoudness = packFloatGainToByte(derived().getAudioLoudnessOut() / AUDIO_LOUDNESS_SCALE);
        destinationBuffer += sizeof(AvatarDataPacket::AudioLoudness);

        int numBytes = destinationBuffer - startSection;
        if (outboundDataRateOut) {
            outboundDataRateOut->audioLoudnessRate.increment(numBytes);
        }
    }

    IF_AVATAR_SPACE(PACKET_HAS_SENSOR_TO_WORLD_MATRIX, sizeof(AvatarDataPacket::SensorToWorldMatrix)) {
        auto startSection = destinationBuffer;

        auto data = reinterpret_cast<AvatarDataPacket::SensorToWorldMatrix*>(destinationBuffer);

        auto matrix = derived().getSensorToWorldMatrixOut();

        // FIXME: CRTP
        // glm::mat4 sensorToWorldMatrix = getSensorToWorldMatrix();
        // rotation = glmExtractRotation(sensorToWorldMatrix);
        // scale = extractScale(sensorToWorldMatrix);
        // translation = sensorToWorldMatrix[3];

        data->sensorToWorldTrans[0] = matrix.translation[0];
        data->sensorToWorldTrans[1] = matrix.translation[1];
        data->sensorToWorldTrans[2] = matrix.translation[2];
        packOrientationQuatToSixBytes(data->sensorToWorldQuat, matrix.rotation);
        packFloatScalarToSignedTwoByteFixed((uint8_t*)&data->sensorToWorldScale, matrix.scale.x, SENSOR_TO_WORLD_SCALE_RADIX);

        destinationBuffer += sizeof(AvatarDataPacket::SensorToWorldMatrix);

        int numBytes = destinationBuffer - startSection;
        if (outboundDataRateOut) {
            outboundDataRateOut->sensorToWorldRate.increment(numBytes);
        }
    }

    IF_AVATAR_SPACE(PACKET_HAS_ADDITIONAL_FLAGS, sizeof (AvatarDataPacket::AdditionalFlags)) {
        auto startSection = destinationBuffer;
        auto data = reinterpret_cast<AvatarDataPacket::AdditionalFlags*>(destinationBuffer);

        uint16_t flags { 0 };

        auto [
            keyState,
            handState,
            headHasScriptedBlendshapes,
            headHasProceduralEyeMovement,
            headHasAudioEnabledFaceMovement,
            headHasProceduralEyeFaceMovement,
            headHasProceduralBlinkFaceMovement, collideWithOtherAvatars,
            hasPriority
        ] = derived().getAdditionalFlagsOut();

        // FIXME: CRTP
        // keyState = _keyState;
        // handState = _handState;
        // headHasScriptedBlendshapes = _headData->_hasScriptedBlendshapes || _headData->_hasInputDrivenBlendshapes
        // headHasProceduralEyeMovement = _headData->getProceduralAnimationFlag(HeadData::SaccadeProceduralEyeJointAnimation) &&
        //     !_headData->getSuppressProceduralAnimationFlag(HeadData::SaccadeProceduralEyeJointAnimation);
        // headHasAudioEnabledFaceMovement = _headData->getProceduralAnimationFlag(HeadData::AudioProceduralBlendshapeAnimation) &&
        //     !_headData->getSuppressProceduralAnimationFlag(HeadData::AudioProceduralBlendshapeAnimation)
        // headHasProceduralEyeFaceMovement = _headData->getProceduralAnimationFlag(HeadData::LidAdjustmentProceduralBlendshapeAnimation) &&
        //     !_headData->getSuppressProceduralAnimationFlag(HeadData::LidAdjustmentProceduralBlendshapeAnimation);
        // headHasProceduralBlinkFaceMovement = _headData->getProceduralAnimationFlag(HeadData::BlinkProceduralBlendshapeAnimation) &&
        //     !_headData->getSuppressProceduralAnimationFlag(HeadData::BlinkProceduralBlendshapeAnimation);
        // collideWithOtherAvatars = _collideWithOtherAvatars;
        // hasPriority = getHasPriority();

        setSemiNibbleAt(flags, KEY_STATE_START_BIT, keyState);

        // hand state
        bool isFingerPointing = handState & IS_FINGER_POINTING_FLAG;
        setSemiNibbleAt(flags, HAND_STATE_START_BIT, handState & ~IS_FINGER_POINTING_FLAG);
        if (isFingerPointing) {
            setAtBit16(flags, HAND_STATE_FINGER_POINTING_BIT);
        }

        // face tracker state
        if (headHasScriptedBlendshapes) {
            setAtBit16(flags, HAS_SCRIPTED_BLENDSHAPES);
        }

        // eye tracker state
        if (headHasProceduralEyeMovement) {
            setAtBit16(flags, HAS_PROCEDURAL_EYE_MOVEMENT);
        }

        bool parentIsNull = std::all_of(parentUUID.begin(), parentUUID.end(),
            std::logical_not<>());
        if (!parentIsNull) {
            setAtBit16(flags, HAS_REFERENTIAL);
        }

        // audio face movement
        if (headHasAudioEnabledFaceMovement) {
            setAtBit16(flags, AUDIO_ENABLED_FACE_MOVEMENT);
        }

        // procedural eye face movement
        if (headHasProceduralEyeFaceMovement) {
            setAtBit16(flags, PROCEDURAL_EYE_FACE_MOVEMENT);
        }

        // procedural blink face movement
        if (headHasProceduralBlinkFaceMovement) {
            setAtBit16(flags, PROCEDURAL_BLINK_FACE_MOVEMENT);
        }

        // avatar collisions enabled
        if (collideWithOtherAvatars) {
            setAtBit16(flags, COLLIDE_WITH_OTHER_AVATARS);
        }

        // Avatar has hero priority
        if (hasPriority) {
            setAtBit16(flags, HAS_HERO_PRIORITY);
        }

        data->flags = flags;
        destinationBuffer += sizeof(AvatarDataPacket::AdditionalFlags);

        int numBytes = destinationBuffer - startSection;
        if (outboundDataRateOut) {
            outboundDataRateOut->additionalFlagsRate.increment(numBytes);
        }
    }

    IF_AVATAR_SPACE(PACKET_HAS_PARENT_INFO, sizeof(AvatarDataPacket::ParentInfo)) {
        auto startSection = destinationBuffer;
        auto parentInfo = reinterpret_cast<AvatarDataPacket::ParentInfo*>(destinationBuffer);

        memcpy(parentInfo->parentUUID, parentUUID.data(), parentUUID.size());
        parentInfo->parentJointIndex = parentJointIndex;
        destinationBuffer += sizeof(AvatarDataPacket::ParentInfo);

        int numBytes = destinationBuffer - startSection;
        if (outboundDataRateOut) {
            outboundDataRateOut->parentInfoRate.increment(numBytes);
        }
    }

    IF_AVATAR_SPACE(PACKET_HAS_AVATAR_LOCAL_POSITION, AvatarDataPacket::AVATAR_LOCAL_POSITION_SIZE) {
        auto startSection = destinationBuffer;
        AvatarDataPacket::AvatarLocalPosition  localPosition = derived().getLocalPositionOut();
        // FIXME: CRTP
        // localPosition = getLocalPosition();
        AVATAR_MEMCPY(localPosition);

        int numBytes = destinationBuffer - startSection;
        if (outboundDataRateOut) {
            outboundDataRateOut->localPositionRate.increment(numBytes);
        }
    }

    IF_AVATAR_SPACE(PACKET_HAS_HAND_CONTROLLERS, AvatarDataPacket::HAND_CONTROLLERS_SIZE) {
        auto startSection = destinationBuffer;

        auto handControllers = derived().getHandControllersOut();

        // FIXME: CRTP
        // Transform controllerLeftHandTransform = Transform(getControllerLeftHandMatrix());
        // Transform controllerRightHandTransform = Transform(getControllerRightHandMatrix());
        // leftRotation = controllerLeftHandTransform.getRotation();
        // leftTranslation = controllerLeftHandTransform.getTranslation();
        // rightRotation = controllerRightHandTransform.getRotation();
        // rightTranslation = controllerRightHandTransform.getTranslation();

        destinationBuffer += packOrientationQuatToSixBytes(destinationBuffer, handControllers.left.orientation);
        destinationBuffer += packFloatVec3ToSignedTwoByteFixed(destinationBuffer, handControllers.left.position, HAND_CONTROLLER_COMPRESSION_RADIX);
        destinationBuffer += packOrientationQuatToSixBytes(destinationBuffer, handControllers.right.orientation);
        destinationBuffer += packFloatVec3ToSignedTwoByteFixed(destinationBuffer, handControllers.right.position, HAND_CONTROLLER_COMPRESSION_RADIX);

        int numBytes = destinationBuffer - startSection;
        if (outboundDataRateOut) {
            outboundDataRateOut->handControllersRate.increment(numBytes);
        }
    }

    IF_AVATAR_SPACE(PACKET_HAS_FACE_TRACKER_INFO, sizeof(AvatarDataPacket::FaceTrackerInfo) + headDataBlendhapeCoefficientsSize * sizeof(float)) {
        auto startSection = destinationBuffer;
        auto faceTrackerInfo = reinterpret_cast<AvatarDataPacket::FaceTrackerInfo*>(destinationBuffer);
        // note: we don't use the blink and average loudness, we just use the numBlendShapes and
        // compute the procedural info on the client side.

        faceTrackerInfo->leftEyeBlink = leftEyeBlink;
        faceTrackerInfo->rightEyeBlink = rightEyeBlink;
        faceTrackerInfo->averageLoudness = averageLoudness;
        faceTrackerInfo->browAudioLift = browAudioLift;

        assert(headDataBlendhapeCoefficientsSize <= std::numeric_limits<uint8_t>::max());
        faceTrackerInfo->numBlendshapeCoefficients = static_cast<uint8_t>(headDataBlendhapeCoefficientsSize);
        destinationBuffer += sizeof(AvatarDataPacket::FaceTrackerInfo);

        auto blendshapeDataSize = headDataBlendhapeCoefficientsSize * sizeof(float);
        memcpy(destinationBuffer, headDataBlendhapeCoefficients, blendshapeDataSize);
        destinationBuffer += blendshapeDataSize;

        int numBytes = destinationBuffer - startSection;
        if (outboundDataRateOut) {
            outboundDataRateOut->faceTrackerRate.increment(numBytes);
        }
    }

    const bool packetHasJointData = wantedFlags & (AvatarDataPacket::PACKET_HAS_JOINT_DATA | AvatarDataPacket::PACKET_HAS_JOINT_DEFAULT_POSE_FLAGS);
    const int numJoints = packetHasJointData ? jointDataSize : 0;

    assert(numJoints <= 255);
    const int jointBitVectorSize = calcBitVectorSize(numJoints);

    // include jointData if there is room for the most minimal section. i.e. no translations or rotations.
    IF_AVATAR_SPACE(PACKET_HAS_JOINT_DATA, AvatarDataPacket::minJointDataSize(numJoints)) {
        auto startSection = destinationBuffer;
        {
            // FIXME: CRTP
            // QReadLocker readLock(&_jointDataLock);

            // Minimum space required for another rotation joint -
            // size of joint + following translation bit-vector + translation scale:
            const ptrdiff_t minSizeForJoint = sizeof(AvatarDataPacket::SixByteQuat) + jointBitVectorSize + sizeof(float);


            // compute maxTranslationDimension before we send any joint data.
            float maxTranslationDimension = 0.001f;
            for (int i = sendStatus.translationsSent; i < numJoints; ++i) {
                const JointData& data = derived().getJointDataOut(i);
                // FIXME: CRTP
                // const JointData& data = _jointData[i];
                if (!data.translationIsDefaultPose) {
                    maxTranslationDimension = glm::max(fabsf(data.translation.x), maxTranslationDimension);
                    maxTranslationDimension = glm::max(fabsf(data.translation.y), maxTranslationDimension);
                    maxTranslationDimension = glm::max(fabsf(data.translation.z), maxTranslationDimension);
                }
            }

            // joint rotation data
            *destinationBuffer++ = (uint8_t)numJoints;

            unsigned char* validityPosition = destinationBuffer;
            memset(validityPosition, 0, jointBitVectorSize);

#ifdef WANT_DEBUG
            int rotationSentCount = 0;
            unsigned char* beforeRotations = destinationBuffer;
#endif

            destinationBuffer += jointBitVectorSize; // Move pointer past the validity bytes

            // sentJointDataOut and lastSentJointData might be the same vector
            if (sentJointDataOut) {
                sentJointDataOut->resize(numJoints); // Make sure the destination is resized before using it
            }
            JointData *const sentJoints = sentJointDataOut ? sentJointDataOut->data() : nullptr;

            float minRotationDOT = (distanceAdjust && cullSmallChanges) ? getDistanceBasedMinRotationDOT(viewerPosition) : AVATAR_MIN_ROTATION_DOT;

            int i = sendStatus.rotationsSent;
            for (; i < numJoints; ++i) {
                const JointData& data = derived().getJointDataOut(i);
                const JointData& last = lastSentJointData[i];

                if (packetEnd - destinationBuffer >= minSizeForJoint) {
                    if (!data.rotationIsDefaultPose) {
                        // The dot product for larger rotations is a lower number,
                        // so if the dot() is less than the value, then the rotation is a larger angle of rotation
                        if (sendAll || last.rotationIsDefaultPose || (!cullSmallChanges && last.rotation != data.rotation)
                            || (cullSmallChanges && fabsf(glm::dot(last.rotation, data.rotation)) < minRotationDOT)) {
                            validityPosition[i / BITS_IN_BYTE] |= 1 << (i % BITS_IN_BYTE);
#ifdef WANT_DEBUG
                            rotationSentCount++;
#endif
                            destinationBuffer += packOrientationQuatToSixBytes(destinationBuffer, data.rotation);
                            if (sentJoints) {
                                sentJoints[i].rotation = data.rotation;
                            }
                        }
                    }
                } else {
                    break;
                }

                if (sentJoints) {
                    sentJoints[i].rotationIsDefaultPose = data.rotationIsDefaultPose;
                }

            }
            sendStatus.rotationsSent = i;

            // joint translation data
            validityPosition = destinationBuffer;

#ifdef WANT_DEBUG
            int translationSentCount = 0;
            unsigned char* beforeTranslations = destinationBuffer;
#endif

            memset(destinationBuffer, 0, jointBitVectorSize);
            destinationBuffer += jointBitVectorSize; // Move pointer past the validity bytes

            // write maxTranslationDimension
            AVATAR_MEMCPY(maxTranslationDimension);

            float minTranslation = (distanceAdjust && cullSmallChanges) ? getDistanceBasedMinTranslationDistance(viewerPosition) : AVATAR_MIN_TRANSLATION;

            i = sendStatus.translationsSent;
            for (; i < numJoints; ++i) {
                const JointData& data = derived().getJointDataOut(i);
                const JointData& last = lastSentJointData[i];

                // Note minSizeForJoint is conservative since there isn't a following bit-vector + scale.
                if (packetEnd - destinationBuffer >= minSizeForJoint) {
                    if (!data.translationIsDefaultPose) {
                        if (sendAll || last.translationIsDefaultPose || (!cullSmallChanges && last.translation != data.translation)
                            || (cullSmallChanges && glm::distance(data.translation, lastSentJointData[i].translation) > minTranslation)) {
                            validityPosition[i / BITS_IN_BYTE] |= 1 << (i % BITS_IN_BYTE);
#ifdef WANT_DEBUG
                            translationSentCount++;
#endif
                            destinationBuffer += packFloatVec3ToSignedTwoByteFixed(destinationBuffer, data.translation / maxTranslationDimension,
                                                                                   TRANSLATION_COMPRESSION_RADIX);

                            if (sentJoints) {
                                sentJoints[i].translation = data.translation;
                            }
                        }
                    }
                } else {
                    break;
                }

                if (sentJoints) {
                    sentJoints[i].translationIsDefaultPose = data.translationIsDefaultPose;
                }

            }
            sendStatus.translationsSent = i;
        }

        IF_AVATAR_SPACE(PACKET_HAS_GRAB_JOINTS, sizeof (AvatarDataPacket::FarGrabJoints)) {

            AvatarDataPacket::FarGrabJoints farGrabJoints = derived().getFarGrabJointsOut();

            // the far-grab joints may range further than 3 meters, so we can't use packFloatVec3ToSignedTwoByteFixed etc
            auto startSection = destinationBuffer;

            // glm::mat4 leftFarGrabMatrix = _farGrabLeftMatrixCache.isValid() ?
            //     _farGrabLeftMatrixCache.get() : glm::mat4();
            // glm::mat4 rightFarGrabMatrix = _farGrabRightMatrixCache.isValid() ?
            //     _farGrabRightMatrixCache.get() : glm::mat4();
            // glm::mat4 mouseFarGrabMatrix = _farGrabMouseMatrixCache.isValid() ?
            //     _farGrabMouseMatrixCache.get() : glm::mat4() ;
            //
            // glm::vec3 leftFarGrabPosition = extractTranslation(leftFarGrabMatrix);
            // glm::quat leftFarGrabRotation = extractRotation(leftFarGrabMatrix);
            // glm::vec3 rightFarGrabPosition = extractTranslation(rightFarGrabMatrix);
            // glm::quat rightFarGrabRotation = extractRotation(rightFarGrabMatrix);
            // glm::vec3 mouseFarGrabPosition = extractTranslation(mouseFarGrabMatrix);
            // glm::quat mouseFarGrabRotation = extractRotation(mouseFarGrabMatrix);
            //
            // farGrabJoints = AvatarDataPacket::FarGrabJoints {
            //     { leftFarGrabPosition.x, leftFarGrabPosition.y, leftFarGrabPosition.z },
            //     { leftFarGrabRotation.w, leftFarGrabRotation.x, leftFarGrabRotation.y, leftFarGrabRotation.z },
            //     { rightFarGrabPosition.x, rightFarGrabPosition.y, rightFarGrabPosition.z },
            //     { rightFarGrabRotation.w, rightFarGrabRotation.x, rightFarGrabRotation.y, rightFarGrabRotation.z },
            //     { mouseFarGrabPosition.x, mouseFarGrabPosition.y, mouseFarGrabPosition.z },
            //     { mouseFarGrabRotation.w, mouseFarGrabRotation.x, mouseFarGrabRotation.y, mouseFarGrabRotation.z }
            // };

            memcpy(destinationBuffer, &farGrabJoints, sizeof(farGrabJoints));
            destinationBuffer += sizeof(AvatarDataPacket::FarGrabJoints);
            int numBytes = destinationBuffer - startSection;

            if (outboundDataRateOut) {
                outboundDataRateOut->farGrabJointRate.increment(numBytes);
            }
        }

#ifdef WANT_DEBUG
        if (sendAll) {
            qCDebug(avatars) << "AvatarDataStream::toByteArray" << cullSmallChanges << sendAll
                << "rotations:" << rotationSentCount << "translations:" << translationSentCount
                << "largest:" << maxTranslationDimension
                << "size:"
                << (beforeRotations - startPosition) << "+"
                << (beforeTranslations - beforeRotations) << "+"
                << (destinationBuffer - beforeTranslations) << "="
                << (destinationBuffer - startPosition);
        }
#endif

        if (sendStatus.rotationsSent != numJoints || sendStatus.translationsSent != numJoints) {
            extraReturnedFlags |= AvatarDataPacket::PACKET_HAS_JOINT_DATA;
        }

        int numBytes = destinationBuffer - startSection;
        if (outboundDataRateOut) {
            outboundDataRateOut->jointDataRate.increment(numBytes);
        }
    }

    IF_AVATAR_SPACE(PACKET_HAS_JOINT_DEFAULT_POSE_FLAGS, 1 + 2 * jointBitVectorSize) {
        auto startSection = destinationBuffer;

        // FIXME: CRTP
        // QReadLocker readLock(&_jointDataLock);

        // write numJoints
        *destinationBuffer++ = (uint8_t)numJoints;

        // write rotationIsDefaultPose bits
        destinationBuffer += writeBitVector(destinationBuffer, numJoints, [&](int i) {
            return derived().getJointDataOut(i).rotationIsDefaultPose;
        });

        // write translationIsDefaultPose bits
        destinationBuffer += writeBitVector(destinationBuffer, numJoints, [&](int i) {
            return derived().getJointDataOut(i).translationIsDefaultPose;
        });

        if (outboundDataRateOut) {
            size_t numBytes = destinationBuffer - startSection;
            outboundDataRateOut->jointDefaultPoseFlagsRate.increment(numBytes);
        }
    }

    memcpy(packetFlagsLocation, &includedFlags, sizeof(includedFlags));
    // Return dropped items.
    sendStatus.itemFlags = (wantedFlags & ~includedFlags) | extraReturnedFlags;

    int avatarDataSize = destinationBuffer - startPosition;

    if (avatarDataSize > (int)byteArraySize) {
        qCCritical(avatars) << "AvatarDataStream::toByteArray buffer overflow"; // We've overflown into the heap
        ASSERT(false);
    }

    return avatarDataByteArray.left(avatarDataSize);

#undef AVATAR_MEMCPY
#undef IF_AVATAR_SPACE
}

template <typename Derived>
void AvatarDataStream<Derived>::doneEncoding(bool cullSmallChanges) {
    const auto jointDataSize = derived().getJointDataSizeOut();
    AvatarDataPacket::SendStatus SendStatus { 0, false, jointDataSize, jointDataSize };
    doneEncoding(cullSmallChanges, SendStatus);
}

// NOTE: This is never used in a "distanceAdjust" mode, so it's ok that it doesn't use a variable minimum rotation/translation
template <typename Derived>
void AvatarDataStream<Derived>::doneEncoding(bool cullSmallChanges, const AvatarDataPacket::SendStatus& sendStatus) {
    // The server has sent some joint-data to other nodes.  Update _lastSentJointData.
    const auto jointDataSize = derived().getJointDataSizeOut();

    // FIXME: CRTP
    // QReadLocker readLock(&_jointDataLock);

    if (jointDataSize > _lastSentJointData.size()) {
        _lastSentJointData.resize(jointDataSize);
    }

    for (int i = 0; i < sendStatus.rotationsSent; ++i) {
        const JointData& data = derived().getJointDataOut(i);
        if (_lastSentJointData[i].rotation != data.rotation) {
            if (!cullSmallChanges ||
                fabsf(glm::dot(data.rotation, _lastSentJointData[i].rotation)) <= AVATAR_MIN_ROTATION_DOT) {
                if (!data.rotationIsDefaultPose) {
                    _lastSentJointData[i].rotation = data.rotation;
                }
            }
        }
    }

    for (int i = 0; i < sendStatus.translationsSent; ++i) {
        const JointData& data = derived().getJointDataOut(i);
        if (_lastSentJointData[i].translation != data.translation) {
            if (!cullSmallChanges ||
                glm::distance(data.translation, _lastSentJointData[i].translation) > AVATAR_MIN_TRANSLATION) {
                if (!data.translationIsDefaultPose) {
                    _lastSentJointData[i].translation = data.translation;
                }
            }
        }
    }

}

// FIXME: CRTP move to base
// void setMatrixCache(ThreadSafeValueCache<glm::mat4>& matrixCache, const HandControllerVantage& vantage) {
//     Transform transform;
//     transform.setTranslation(vantage.position);
//     transform.setRotation(vantage.orientation);
//     matrixCache.set(transform.getMatrix());
// }

inline HandControllerVantage unpackHandController(const unsigned char*& sourceBuffer) {
    HandControllerVantage vantage{};
    sourceBuffer += unpackOrientationQuatFromSixBytes(sourceBuffer, vantage.orientation);
    sourceBuffer += unpackFloatVec3FromSignedTwoByteFixed(sourceBuffer, vantage.position, HAND_CONTROLLER_COMPRESSION_RADIX);
    return vantage;
}


// read data in packet starting at byte offset and return number of bytes parsed
template <typename Derived>
int AvatarDataStream<Derived>::parseDataFromBuffer(const QByteArray& buffer) {

    // FIXME: CRTP move to derived
    // lazily allocate memory for HeadData in case we're not an Avatar instance
    // lazyInitHeadData();

    // FIXME: CRTP
    // parentID = getParentID();

    AvatarDataPacket::HasFlags packetStateFlags;

    const unsigned char* startPosition = reinterpret_cast<const unsigned char*>(buffer.data());
    const unsigned char* endPosition = startPosition + buffer.size();
    const unsigned char* sourceBuffer = startPosition;

    auto packetReadCheck = [&](const char* name, int sizeToRead) {
        if ((endPosition - sourceBuffer) < (int)sizeToRead) {
            derived().onPacketTooSmallError(name, sizeToRead, endPosition - sourceBuffer);
            // FIXME: CRTP
            // if (shouldLogError(now)) {
            //     qCWarning(avatars) << "Avatar data packet too small, attempting to read " <<
            //         #ITEM_NAME << ", only " << (endPosition - sourceBuffer) <<
            //         " bytes left, " << getSessionUUID();
            // }
            return false;
        }
        return true;
    };

    // read the packet flags
    memcpy(&packetStateFlags, sourceBuffer, sizeof(packetStateFlags));
    sourceBuffer += sizeof(packetStateFlags);

    #define HAS_FLAG(B,F) ((B & F) == F)

    bool hasAvatarGlobalPosition  = HAS_FLAG(packetStateFlags, AvatarDataPacket::PACKET_HAS_AVATAR_GLOBAL_POSITION);
    bool hasAvatarBoundingBox     = HAS_FLAG(packetStateFlags, AvatarDataPacket::PACKET_HAS_AVATAR_BOUNDING_BOX);
    bool hasAvatarOrientation     = HAS_FLAG(packetStateFlags, AvatarDataPacket::PACKET_HAS_AVATAR_ORIENTATION);
    bool hasAvatarScale           = HAS_FLAG(packetStateFlags, AvatarDataPacket::PACKET_HAS_AVATAR_SCALE);
    bool hasLookAtPosition        = HAS_FLAG(packetStateFlags, AvatarDataPacket::PACKET_HAS_LOOK_AT_POSITION);
    bool hasAudioLoudness         = HAS_FLAG(packetStateFlags, AvatarDataPacket::PACKET_HAS_AUDIO_LOUDNESS);
    bool hasSensorToWorldMatrix   = HAS_FLAG(packetStateFlags, AvatarDataPacket::PACKET_HAS_SENSOR_TO_WORLD_MATRIX);
    bool hasAdditionalFlags       = HAS_FLAG(packetStateFlags, AvatarDataPacket::PACKET_HAS_ADDITIONAL_FLAGS);
    bool hasParentInfo            = HAS_FLAG(packetStateFlags, AvatarDataPacket::PACKET_HAS_PARENT_INFO);
    bool hasAvatarLocalPosition   = HAS_FLAG(packetStateFlags, AvatarDataPacket::PACKET_HAS_AVATAR_LOCAL_POSITION);
    bool hasHandControllers       = HAS_FLAG(packetStateFlags, AvatarDataPacket::PACKET_HAS_HAND_CONTROLLERS);
    bool hasFaceTrackerInfo       = HAS_FLAG(packetStateFlags, AvatarDataPacket::PACKET_HAS_FACE_TRACKER_INFO);
    bool hasJointData             = HAS_FLAG(packetStateFlags, AvatarDataPacket::PACKET_HAS_JOINT_DATA);
    bool hasJointDefaultPoseFlags = HAS_FLAG(packetStateFlags, AvatarDataPacket::PACKET_HAS_JOINT_DEFAULT_POSE_FLAGS);
    bool hasGrabJoints            = HAS_FLAG(packetStateFlags, AvatarDataPacket::PACKET_HAS_GRAB_JOINTS);

    // FIXME: CRTP will be needed in derived
    // quint64 now = usecTimestampNow();

    if (hasAvatarGlobalPosition) {
        auto startSection = sourceBuffer;

        if (!packetReadCheck("AvatarGlobalPosition", sizeof(AvatarDataPacket::AvatarGlobalPosition))) {
            return buffer.size();
        }

        auto data = reinterpret_cast<const AvatarDataPacket::AvatarGlobalPosition*>(sourceBuffer);

        derived().setGlobalPositionIn(*data);

        // FIXME: CRTP move to Derived::setGlobalPositionIn;
        // glm::vec3 offset = glm::vec3(0.0f, 0.0f, 0.0f);
        // if (_replicaIndex > 0) {
        //     const float SPACE_BETWEEN_AVATARS = 2.0f;
        //     const int AVATARS_PER_ROW = 3;
        //     int row = _replicaIndex % AVATARS_PER_ROW;
        //     int col = floor(_replicaIndex / AVATARS_PER_ROW);
        //     offset = glm::vec3(row * SPACE_BETWEEN_AVATARS, 0.0f, col * SPACE_BETWEEN_AVATARS);
        // }
        // _serverPosition = glm::vec3(data->globalPosition[0], data->globalPosition[1], data->globalPosition[2]) + offset;
        // if (_isClientAvatar) {
        //     auto oneStepDistance = glm::length(_globalPosition - _serverPosition);
        //     if (oneStepDistance <= AVATAR_TRANSIT_MIN_TRIGGER_DISTANCE || oneStepDistance >= AVATAR_TRANSIT_MAX_TRIGGER_DISTANCE) {
        //         _globalPosition = _serverPosition;
        //         // if we don't have a parent, make sure to also set our local position
        //         if (parentID.isNull()) {
        //             setLocalPosition(_serverPosition);
        //         }
        //     }
        //     if (_globalPosition != _serverPosition) {
        //         _globalPositionChanged = now;
        //     }
        // } else {
        //     if (_globalPosition != _serverPosition) {
        //         _globalPosition = _serverPosition;
        //         _globalPositionChanged = now;
        //     }
        //     if (parentID.isNull()) {
        //         setLocalPosition(_serverPosition);
        //     }
        // }

        sourceBuffer += sizeof(AvatarDataPacket::AvatarGlobalPosition);
        int numBytesRead = sourceBuffer - startSection;
        _globalPositionRate.increment(numBytesRead);
        _globalPositionUpdateRate.increment();
    }

    if (hasAvatarBoundingBox) {
        auto startSection = sourceBuffer;

        if (!packetReadCheck("AvatarBoundingBox", sizeof(AvatarDataPacket::AvatarBoundingBox))) {
            return buffer.size();
        }

        auto data = reinterpret_cast<const AvatarDataPacket::AvatarBoundingBox*>(sourceBuffer);

        derived().setBoundingBoxIn(*data);
        // FIXME: CRTP
        // auto newDimensions = glm::vec3(data->avatarDimensions[0], data->avatarDimensions[1], data->avatarDimensions[2]);
        // auto newOffset = glm::vec3(data->boundOriginOffset[0], data->boundOriginOffset[1], data->boundOriginOffset[2]);
        // if (_globalBoundingBoxDimensions != newDimensions) {
        //     _globalBoundingBoxDimensions = newDimensions;
        //     _avatarBoundingBoxChanged = now;
        // }
        // if (_globalBoundingBoxOffset != newOffset) {
        //     _globalBoundingBoxOffset = newOffset;
        //     _avatarBoundingBoxChanged = now;
        // }
        // _defaultBubbleBox = computeBubbleBox();

        sourceBuffer += sizeof(AvatarDataPacket::AvatarBoundingBox);
        int numBytesRead = sourceBuffer - startSection;
        _avatarBoundingBoxRate.increment(numBytesRead);
        _avatarBoundingBoxUpdateRate.increment();
    }

    if (hasAvatarOrientation) {
        auto startSection = sourceBuffer;
        if (!packetReadCheck("AvatarOrientation", sizeof(AvatarDataPacket::AvatarOrientation))) {
            return buffer.size();
        }
        glm::quat newOrientation;
        sourceBuffer += unpackOrientationQuatFromSixBytes(sourceBuffer, newOrientation);

        derived().setOrientationIn(newOrientation);
        // FIXME: CRTP
        // glm::quat currentOrientation = getLocalOrientation();
        // if (currentOrientation != newOrientation) {
        //     _hasNewJointData = true;
        //     setLocalOrientation(newOrientation);
        // }

        int numBytesRead = sourceBuffer - startSection;
        _avatarOrientationRate.increment(numBytesRead);
        _avatarOrientationUpdateRate.increment();
    }

    if (hasAvatarScale) {
        auto startSection = sourceBuffer;

        if (!packetReadCheck("AvatarScale", sizeof(AvatarDataPacket::AvatarScale))) {
            return buffer.size();
        }

        auto data = reinterpret_cast<const AvatarDataPacket::AvatarScale*>(sourceBuffer);
        float scale;
        unpackFloatRatioFromTwoByte((uint8_t*)&data->scale, scale);
        if (isNaN(scale)) {
            derived().onParseError("Discard avatar data packet: scale NaN.");
            // FIXME: CRTP
            // if (shouldLogError(now)) {
            //     qCWarning(avatars) << "Discard avatar data packet: scale NaN, uuid " << getSessionUUID();
            // }
            return buffer.size();
        }
        derived().setScaleIn(scale);
        // FIXME: CRTP
        // setTargetScale(scale);
        sourceBuffer += sizeof(AvatarDataPacket::AvatarScale);
        int numBytesRead = sourceBuffer - startSection;
        _avatarScaleRate.increment(numBytesRead);
        _avatarScaleUpdateRate.increment();
    }

    if (hasLookAtPosition) {
        auto startSection = sourceBuffer;

        if (!packetReadCheck("LookAtPosition", sizeof(AvatarDataPacket::LookAtPosition))) {
            return buffer.size();
        }

        auto data = reinterpret_cast<const AvatarDataPacket::LookAtPosition*>(sourceBuffer);
        if (std::any_of(+data->lookAtPosition.data, data->lookAtPosition.data + 3, (bool(*)(float))isNaN)) {
            derived().onParseError("Discard avatar data packet: lookAtPosition is NaN");
            // FIXME: CRTP
            // if (shouldLogError(now)) {
            //     qCWarning(avatars) << "Discard avatar data packet: lookAtPosition is NaN, uuid " << getSessionUUID();
            // }
            return buffer.size();
        }
        derived().setLookAtPositionIn(*data);
        // FIXME: CRTP
        // glm::vec3 lookAt = glm::vec3(data->lookAtPosition[0], data->lookAtPosition[1], data->lookAtPosition[2]);
        // _headData->setLookAtPosition(lookAt);
        sourceBuffer += sizeof(AvatarDataPacket::LookAtPosition);
        int numBytesRead = sourceBuffer - startSection;
        _lookAtPositionRate.increment(numBytesRead);
        _lookAtPositionUpdateRate.increment();
    }

    if (hasAudioLoudness) {
        auto startSection = sourceBuffer;

        if (!packetReadCheck("AudioLoudness", sizeof(AvatarDataPacket::AudioLoudness))) {
            return buffer.size();
        }

        auto data = reinterpret_cast<const AvatarDataPacket::AudioLoudness*>(sourceBuffer);
        float audioLoudness;
        audioLoudness = unpackFloatGainFromByte(data->audioLoudness) * AUDIO_LOUDNESS_SCALE;
        sourceBuffer += sizeof(AvatarDataPacket::AudioLoudness);

        if (isNaN(audioLoudness)) {
            derived().onParseError("Discard avatar data packet: audioLoudness is NaN");
            // FIXME: CRTP
            // if (shouldLogError(now)) {
            //     qCWarning(avatars) << "Discard avatar data packet: audioLoudness is NaN, uuid " << getSessionUUID();
            // }
            return buffer.size();
        }
        // FIXME: CRTP
        // setAudioLoudness(audioLoudness);
        derived().setAudioLoudnessIn(audioLoudness);
        int numBytesRead = sourceBuffer - startSection;
        _audioLoudnessRate.increment(numBytesRead);
        _audioLoudnessUpdateRate.increment();
    }

    if (hasSensorToWorldMatrix) {
        auto startSection = sourceBuffer;

        if (!packetReadCheck("SensorToWorldMatrix", sizeof(AvatarDataPacket::SensorToWorldMatrix))) {
            return buffer.size();
        }

        auto data = reinterpret_cast<const AvatarDataPacket::SensorToWorldMatrix*>(sourceBuffer);
        glm::quat sensorToWorldQuat;
        unpackOrientationQuatFromSixBytes(data->sensorToWorldQuat, sensorToWorldQuat);
        float sensorToWorldScale;
        // Grab a local copy of sensorToWorldScale to be able to use the unpack function with a pointer on it,
        // a direct pointer on the struct attribute triggers warnings because of potential misalignement.
        auto srcSensorToWorldScale = data->sensorToWorldScale;
        unpackFloatScalarFromSignedTwoByteFixed((int16_t*)&srcSensorToWorldScale, &sensorToWorldScale, SENSOR_TO_WORLD_SCALE_RADIX);
        glm::vec3 sensorToWorldTrans(data->sensorToWorldTrans[0], data->sensorToWorldTrans[1], data->sensorToWorldTrans[2]);

        derived().setSensorToWorldMatrixIn({
            sensorToWorldTrans,
            sensorToWorldQuat,
            glm::vec3(sensorToWorldScale)
        });
        // FIXME: CRTP
        // glm::mat4 sensorToWorldMatrix = createMatFromScaleQuatAndPos(glm::vec3(sensorToWorldScale), sensorToWorldQuat, sensorToWorldTrans);
        // if (_sensorToWorldMatrixCache.get() != sensorToWorldMatrix) {
        //     _sensorToWorldMatrixCache.set(sensorToWorldMatrix);
        //     _sensorToWorldMatrixChanged = now;
        // }

        sourceBuffer += sizeof(AvatarDataPacket::SensorToWorldMatrix);
        int numBytesRead = sourceBuffer - startSection;
        _sensorToWorldRate.increment(numBytesRead);
        _sensorToWorldUpdateRate.increment();
    }

    if (hasAdditionalFlags) {
        auto startSection = sourceBuffer;

        if (!packetReadCheck("AdditionalFlags", sizeof(AvatarDataPacket::AdditionalFlags))) {
            return buffer.size();
        }

        auto data = reinterpret_cast<const AvatarDataPacket::AdditionalFlags*>(sourceBuffer);
        uint16_t bitItems = data->flags;

        // key state, stored as a semi-nibble in the bitItems
        auto newKeyState = (KeyState)getSemiNibbleAt(bitItems, KEY_STATE_START_BIT);

        // hand state, stored as a semi-nibble plus a bit in the bitItems
        // we store the hand state as well as other items in a shared bitset. The hand state is an octal, but is split
        // into two sections to maintain backward compatibility. The bits are ordered as such (0-7 left to right).
        // AA 6/1/18 added three more flags bits 8,9, and 10 for procedural audio, blink, and eye saccade enabled
        //     +---+-----+-----+--+--+--+--+--+----+
        //     |x,x|H0,H1|x,x,x|H2|Au|Bl|Ey|He|xxxx|
        //     +---+-----+-----+--+--+--+--+--+----+
        // Hand state - H0,H1,H2 is found in the 3rd, 4th, and 8th bits
        // Hero-avatar status (He) - 12th bit
        auto newHandState = getSemiNibbleAt(bitItems, HAND_STATE_START_BIT)
            + (oneAtBit16(bitItems, HAND_STATE_FINGER_POINTING_BIT) ? IS_FINGER_POINTING_FLAG : 0);

        auto newHasScriptedBlendshapes = oneAtBit16(bitItems, HAS_SCRIPTED_BLENDSHAPES);
        auto newHasProceduralEyeMovement = oneAtBit16(bitItems, HAS_PROCEDURAL_EYE_MOVEMENT);
        auto newHasAudioEnabledFaceMovement = oneAtBit16(bitItems, AUDIO_ENABLED_FACE_MOVEMENT);
        auto newHasProceduralEyeFaceMovement = oneAtBit16(bitItems, PROCEDURAL_EYE_FACE_MOVEMENT);
        auto newHasProceduralBlinkFaceMovement = oneAtBit16(bitItems, PROCEDURAL_BLINK_FACE_MOVEMENT);

        auto newCollideWithOtherAvatars = oneAtBit16(bitItems, COLLIDE_WITH_OTHER_AVATARS);
        auto newHasPriority = oneAtBit16(bitItems, HAS_HERO_PRIORITY);

        derived().setAdditionalFlagsIn({
            newKeyState,
            static_cast<uint8_t>(newHandState),
            newHasScriptedBlendshapes,
            newHasProceduralEyeMovement,
            newHasAudioEnabledFaceMovement,
            newHasProceduralEyeFaceMovement,
            newHasProceduralBlinkFaceMovement,
            newCollideWithOtherAvatars,
            newHasPriority
        });

        // FIXME: CRTP
        // KeyState keyState {};
        // keyState = _keyState;

        // FIXME: CRTP
        // char handState {};
        // handState = _handState;

        //FIXME: CRTP
        // bool headDataHasScriptedBlendshaped = false;
        // bool headDataHasProceduralEyeMovement = false;
        // bool headDataHasAudioEnabledFaceMovement = false;
        // bool headDataHasProceduralEyeFaceMovement = false;
        // bool headDataHasProceduralBlinkFaceMovement = false;
        // bool collideWithOtherAvatars = false;
        // bool hasPriority = false;

        // headDataHasScriptedBlendshaped = _headData->getHasScriptedBlendshapes();
        // headDataHasProceduralEyeMovement = _headData->getProceduralAnimationFlag(HeadData::SaccadeProceduralEyeJointAnimation);
        // headDataHasAudioEnabledFaceMovement = _headData->getProceduralAnimationFlag(HeadData::AudioProceduralBlendshapeAnimation);
        // hasProceduralEyeFaceMovement = _headData->getProceduralAnimationFlag(HeadData::LidAdjustmentProceduralBlendshapeAnimation);
        // hasProceduralBlinkFaceMovement = _headData->getProceduralAnimationFlag(HeadData::BlinkProceduralBlendshapeAnimation);
        // collideWithOtherAvatars = _collideWithOtherAvatars;
        // hasPriority = getHasPriority();

        // bool keyStateChanged = (keyState != newKeyState);
        // bool handStateChanged = (handState != newHandState);
        // bool faceStateChanged = (headDataHasScriptedBlendshaped != newHasScriptedBlendshapes);

        // bool eyeStateChanged = (headDataHasProceduralEyeMovement != newHasProceduralEyeMovement);
        // bool audioEnableFaceMovementChanged = (headDataHasAudioEnabledFaceMovement != newHasAudioEnabledFaceMovement);
        // bool proceduralEyeFaceMovementChanged = (headDataHasProceduralEyeFaceMovement != newHasProceduralEyeFaceMovement);
        // bool proceduralBlinkFaceMovementChanged = (headDataHasProceduralBlinkFaceMovement != newHasProceduralBlinkFaceMovement);
        // bool collideWithOtherAvatarsChanged = (collideWithOtherAvatars != newCollideWithOtherAvatars);
        // bool hasPriorityChanged = (hasPriority != newHasPriority);
        // bool somethingChanged = keyStateChanged || handStateChanged || faceStateChanged || eyeStateChanged || audioEnableFaceMovementChanged ||
        //                         proceduralEyeFaceMovementChanged ||
        //                         proceduralBlinkFaceMovementChanged || collideWithOtherAvatarsChanged || hasPriorityChanged;

        // FIXME: CRTP
        // _keyState = newKeyState;
        // _handState = newHandState;
        // TODO: this really belongs in headData
        // if (!newHasScriptedBlendshapes && headDataHasScriptedBlendshaped) {
        //     // if scripted blendshapes have just been turned off, slam blendshapes back to zero.
        //     _headData->clearBlendshapeCoefficients();
        // }
        // _headData->setHasScriptedBlendshapes(newHasScriptedBlendshapes);
        // _headData->setProceduralAnimationFlag(HeadData::SaccadeProceduralEyeJointAnimation, newHasProceduralEyeMovement);
        // _headData->setProceduralAnimationFlag(HeadData::AudioProceduralBlendshapeAnimation, newHasAudioEnabledFaceMovement);
        // _headData->setProceduralAnimationFlag(HeadData::LidAdjustmentProceduralBlendshapeAnimation, newHasProceduralEyeFaceMovement);
        // _headData->setProceduralAnimationFlag(HeadData::BlinkProceduralBlendshapeAnimation, newHasProceduralBlinkFaceMovement);
        // _collideWithOtherAvatars = newCollideWithOtherAvatars;
        // setHasPriorityWithoutTimestampReset(newHasPriority);

        sourceBuffer += sizeof(AvatarDataPacket::AdditionalFlags);

        // FIXME: CRTP
        // if (collideWithOtherAvatarsChanged) {
        //     setCollisionWithOtherAvatarsFlags();
        // }
        // if (somethingChanged) {
        //     _additionalFlagsChanged = now;
        // }
        int numBytesRead = sourceBuffer - startSection;
        _additionalFlagsRate.increment(numBytesRead);
        _additionalFlagsUpdateRate.increment();
    }

    if (hasParentInfo) {
        auto startSection = sourceBuffer;

        if (!packetReadCheck("ParentInfo", sizeof(AvatarDataPacket::ParentInfo))) {
            return buffer.size();
        }

        auto parentInfo = reinterpret_cast<const AvatarDataPacket::ParentInfo*>(sourceBuffer);
        sourceBuffer += sizeof(AvatarDataPacket::ParentInfo);

        derived().setParentInfoIn(parentInfo->parentUUID, parentInfo->parentJointIndex);
        // FIXME: CRTP
        // QByteArray byteArray((const char*)parentInfo->parentUUID, NUM_BYTES_RFC4122_UUID);
        // auto newParentID = QUuid::fromRfc4122(byteArray);
        // if ((getParentID() != newParentID) || (getParentJointIndex() != parentInfo->parentJointIndex)) {
        //     SpatiallyNestable::setParentID(newParentID);
        //     SpatiallyNestable::setParentJointIndex(parentInfo->parentJointIndex);
        //     _parentChanged = now;
        // }

        int numBytesRead = sourceBuffer - startSection;
        _parentInfoRate.increment(numBytesRead);
        _parentInfoUpdateRate.increment();
    }

    if (hasAvatarLocalPosition) {
        auto startSection = sourceBuffer;

        if (!packetReadCheck("AvatarLocalPosition", sizeof(AvatarDataPacket::AvatarLocalPosition))) {
            return buffer.size();
        }

        auto data = reinterpret_cast<const AvatarDataPacket::AvatarLocalPosition*>(sourceBuffer);

        if (std::any_of(+data->localPosition.data, data->localPosition.data + 3, (bool(*)(float))isNaN)) {
            derived().onParseError("Discard avatar data packet: position NaN.");
            // FIXME: CRTP
            // if (shouldLogError(now)) {
            //     qCWarning(avatars) << "Discard avatar data packet: position NaN, uuid " << getSessionUUID();
            // }
            return buffer.size();
        }
        derived().setLocalPositionIn(*data);
        // FIXME: CRTP
        // glm::vec3 position = glm::vec3(data->localPosition[0], data->localPosition[1], data->localPosition[2]);
        // if (parentID.isNull()) {
        //     qCWarning(avatars) << "received localPosition for avatar with no parent";
        // } else {
        //     setLocalPosition(position);
        // }
        sourceBuffer += sizeof(AvatarDataPacket::AvatarLocalPosition);
        int numBytesRead = sourceBuffer - startSection;
        _localPositionRate.increment(numBytesRead);
        _localPositionUpdateRate.increment();
    }

    if (hasHandControllers) {
        auto startSection = sourceBuffer;


        derived().setHandControllersIn({
            unpackHandController(sourceBuffer),
            unpackHandController(sourceBuffer)
        });

        // FIXME: CRTP
        // setMatrixCache(_controllerLeftHandMatrixCache, leftVantage);
        // setMatrixCache(_controllerRightHandMatrixCache, rightVantage);

        int numBytesRead = sourceBuffer - startSection;
        _handControllersRate.increment(numBytesRead);
        _handControllersUpdateRate.increment();
    } else {
        // FIXME: CRTP
        // _controllerLeftHandMatrixCache.invalidate();
        // _controllerRightHandMatrixCache.invalidate();
    }

    if (hasFaceTrackerInfo) {
        auto startSection = sourceBuffer;

        if (!packetReadCheck("FaceTrackerInfo", sizeof(AvatarDataPacket::FaceTrackerInfo))) {
            return buffer.size();
        }

        auto faceTrackerInfo = reinterpret_cast<const AvatarDataPacket::FaceTrackerInfo*>(sourceBuffer);
        sourceBuffer += sizeof(AvatarDataPacket::FaceTrackerInfo);

        const int coefficientsSize = sizeof(float) * faceTrackerInfo->numBlendshapeCoefficients;

        if (!packetReadCheck("FaceTrackerCoefficients", coefficientsSize)) {
            return buffer.size();
        }

        derived().setFaceTrackerInfoIn({
            faceTrackerInfo->leftEyeBlink,
            faceTrackerInfo->rightEyeBlink,
            faceTrackerInfo->averageLoudness,
            faceTrackerInfo->browAudioLift,
            faceTrackerInfo->numBlendshapeCoefficients,
            sourceBuffer
        });

        // TODO: CRTP
        // int numCoefficients = faceTrackerInfo->numBlendshapeCoefficients;
        // const int coefficientsSize = sizeof(float) * numCoefficients;
        // _headData->_blendshapeCoefficients.resize(std::min(numCoefficients, (int)Blendshapes::BlendshapeCount));  // make sure there's room for the copy!
        //only copy the blendshapes to headData, not the procedural face info
        // memcpy(_headData->_blendshapeCoefficients.data(), sourceBuffer, coefficientsSize);

        sourceBuffer += coefficientsSize;

        int numBytesRead = sourceBuffer - startSection;
        _faceTrackerRate.increment(numBytesRead);
        _faceTrackerUpdateRate.increment();
    }

    if (hasJointData) {
        auto startSection = sourceBuffer;

        if (!packetReadCheck("NumJoints", sizeof(uint8_t))) {
            return buffer.size();
        }

        int numJoints = *sourceBuffer++;
        const int bytesOfValidity = (int)ceil((float)numJoints / (float)BITS_IN_BYTE);

        if (!packetReadCheck("JointRotationValidityBits", bytesOfValidity)) {
            return buffer.size();
        }

        int numValidJointRotations = 0;
        QVector<bool> validRotations;
        validRotations.resize(numJoints);
        { // rotation validity bits
            unsigned char validity = 0;
            int validityBit = 0;
            for (int i = 0; i < numJoints; i++) {
                if (validityBit == 0) {
                    validity = *sourceBuffer++;
                }
                bool valid = (bool)(validity & (1 << validityBit));
                if (valid) {
                    ++numValidJointRotations;
                }
                validRotations[i] = valid;
                validityBit = (validityBit + 1) % BITS_IN_BYTE;
            }
        }

        {
            // FIXME: CRTP
            // QWriteLocker writeLock(&_jointDataLock);
            derived().setJointDataSizeIn(numJoints);

            // each joint rotation is stored in 6 bytes.
            const int COMPRESSED_QUATERNION_SIZE = 6;
            if (!packetReadCheck("JointRotations", numValidJointRotations * COMPRESSED_QUATERNION_SIZE)) {
                return buffer.size();
            }

            for (int i = 0; i < numJoints; i++) {
                glm::quat rotation{};
                if (validRotations[i]) {
                    sourceBuffer += unpackOrientationQuatFromSixBytes(sourceBuffer, rotation);
                    _hasNewJointData = true;
                    derived().setJointDataRotationIn(i, rotation);
                    derived().setJointDataRotationDefaultIn(i, false);
                }
            }

            if (!packetReadCheck("JointTranslationValidityBits", bytesOfValidity)) {
                return buffer.size();
            }

            // get translation validity bits -- these indicate which translations were packed
            int numValidJointTranslations = 0;
            QVector<bool> validTranslations;
            validTranslations.resize(numJoints);
            { // translation validity bits
                unsigned char validity = 0;
                int validityBit = 0;
                for (int i = 0; i < numJoints; i++) {
                    if (validityBit == 0) {
                        validity = *sourceBuffer++;
                    }
                    bool valid = (bool)(validity & (1 << validityBit));
                    if (valid) {
                        ++numValidJointTranslations;
                    }
                    validTranslations[i] = valid;
                    validityBit = (validityBit + 1) % BITS_IN_BYTE;
                }
            } // 1 + bytesOfValidity bytes

            // read maxTranslationDimension
            float maxTranslationDimension;

            if (!packetReadCheck("JointMaxTranslationDimension", sizeof(float))) {
                return buffer.size();
            }

            memcpy(&maxTranslationDimension, sourceBuffer, sizeof(float));
            sourceBuffer += sizeof(float);

            // each joint translation component is stored in 6 bytes.
            const int COMPRESSED_TRANSLATION_SIZE = 6;
            if (!packetReadCheck("JointTranslation", numValidJointTranslations * COMPRESSED_TRANSLATION_SIZE)) {
                return buffer.size();
            }

            for (int i = 0; i < numJoints; i++) {
                glm::vec3 translation{};
                if (validTranslations[i]) {
                    sourceBuffer += unpackFloatVec3FromSignedTwoByteFixed(sourceBuffer, translation, TRANSLATION_COMPRESSION_RADIX);
                    translation *= maxTranslationDimension;
                    derived().setJointDataPositionIn(i, translation);
                    derived().setJointDataPositionDefaultIn(i, false);
                    _hasNewJointData = true;
                }
            }

#ifdef WANT_DEBUG
            if (numValidJointRotations > 15) {
                qCDebug(avatars) << "RECEIVING -- rotations:" << numValidJointRotations
                    << "translations:" << numValidJointTranslations
                    << "size:" << (int)(sourceBuffer - startPosition);
            }
#endif
            int numBytesRead = sourceBuffer - startSection;
            _jointDataRate.increment(numBytesRead);
            _jointDataUpdateRate.increment();
        }

        if (hasGrabJoints) {
            auto startSection = sourceBuffer;

            if (!packetReadCheck("FarGrabJoints", sizeof(AvatarDataPacket::FarGrabJoints))) {
                return buffer.size();
            }

            AvatarDataPacket::FarGrabJoints farGrabJoints;
            memcpy(&farGrabJoints, sourceBuffer, sizeof(farGrabJoints)); // to avoid misaligned floats

            derived().setFarGrabJointsIn(farGrabJoints);

            // FIXME: CRTP
            // glm::vec3 leftFarGrabPosition = glm::vec3(farGrabJoints.leftFarGrabPosition[0],
            //                                           farGrabJoints.leftFarGrabPosition[1],
            //                                           farGrabJoints.leftFarGrabPosition[2]);
            // glm::quat leftFarGrabRotation = glm::quat(farGrabJoints.leftFarGrabRotation[0],
            //                                           farGrabJoints.leftFarGrabRotation[1],
            //                                           farGrabJoints.leftFarGrabRotation[2],
            //                                           farGrabJoints.leftFarGrabRotation[3]);
            // glm::vec3 rightFarGrabPosition = glm::vec3(farGrabJoints.rightFarGrabPosition[0],
            //                                            farGrabJoints.rightFarGrabPosition[1],
            //                                            farGrabJoints.rightFarGrabPosition[2]);
            // glm::quat rightFarGrabRotation = glm::quat(farGrabJoints.rightFarGrabRotation[0],
            //                                            farGrabJoints.rightFarGrabRotation[1],
            //                                            farGrabJoints.rightFarGrabRotation[2],
            //                                            farGrabJoints.rightFarGrabRotation[3]);
            // glm::vec3 mouseFarGrabPosition = glm::vec3(farGrabJoints.mouseFarGrabPosition[0],
            //                                            farGrabJoints.mouseFarGrabPosition[1],
            //                                            farGrabJoints.mouseFarGrabPosition[2]);
            // glm::quat mouseFarGrabRotation = glm::quat(farGrabJoints.mouseFarGrabRotation[0],
            //                                            farGrabJoints.mouseFarGrabRotation[1],
            //                                            farGrabJoints.mouseFarGrabRotation[2],
            //                                            farGrabJoints.mouseFarGrabRotation[3]);
            //
            // _farGrabLeftMatrixCache.set(createMatFromQuatAndPos(leftFarGrabRotation, leftFarGrabPosition));
            // _farGrabRightMatrixCache.set(createMatFromQuatAndPos(rightFarGrabRotation, rightFarGrabPosition));
            // _farGrabMouseMatrixCache.set(createMatFromQuatAndPos(mouseFarGrabRotation, mouseFarGrabPosition));

            sourceBuffer += sizeof(AvatarDataPacket::FarGrabJoints);
            int numBytesRead = sourceBuffer - startSection;
            _farGrabJointRate.increment(numBytesRead);
            _farGrabJointUpdateRate.increment();
        }
    }

    if (hasJointDefaultPoseFlags) {
        auto startSection = sourceBuffer;

        // FIXME: CRTP
        // QWriteLocker writeLock(&_jointDataLock);

        if (!packetReadCheck("JointDefaultPoseFlagsNumJoints", sizeof(uint8_t))) {
            return buffer.size();
        }

        int numJoints = (int)*sourceBuffer++;

        derived().setJointDataSizeIn(numJoints);

        int bitVectorSize = calcBitVectorSize(numJoints);

        if (!packetReadCheck("JointDefaultPoseFlagsRotationFlags", bitVectorSize)) {
            return buffer.size();
        }

        sourceBuffer += readBitVector(sourceBuffer, numJoints, [&](int i, bool value) {
            derived().setJointDataRotationDefaultIn(i, value);
        });

        if (!packetReadCheck("JointDefaultPoseFlagsTranslationFlags", bitVectorSize)) {
            return buffer.size();
        }
        sourceBuffer += readBitVector(sourceBuffer, numJoints, [&](int i, bool value) {
            derived().setJointDataPositionDefaultIn(i, value);
        });

        int numBytesRead = sourceBuffer - startSection;
        _jointDefaultPoseFlagsRate.increment(numBytesRead);
        _jointDefaultPoseFlagsUpdateRate.increment();
    }

    int numBytesRead = sourceBuffer - startPosition;
    _averageBytesReceived.updateAverage(numBytesRead);

    _parseBufferRate.increment(numBytesRead);
    _parseBufferUpdateRate.increment();

    return numBytesRead;
}

template <typename Derived>
float AvatarDataStream<Derived>::getDataRate(const QString& rateName) const {
    if (rateName == "") {
        return _parseBufferRate.rate() / BYTES_PER_KILOBIT;
    } else if (rateName == "globalPosition") {
        return _globalPositionRate.rate() / BYTES_PER_KILOBIT;
    } else if (rateName == "localPosition") {
        return _localPositionRate.rate() / BYTES_PER_KILOBIT;
    } else if (rateName == "handControllers") {
        return _handControllersRate.rate() / BYTES_PER_KILOBIT;
    } else if (rateName == "avatarBoundingBox") {
        return _avatarBoundingBoxRate.rate() / BYTES_PER_KILOBIT;
    } else if (rateName == "avatarOrientation") {
        return _avatarOrientationRate.rate() / BYTES_PER_KILOBIT;
    } else if (rateName == "avatarScale") {
        return _avatarScaleRate.rate() / BYTES_PER_KILOBIT;
    } else if (rateName == "lookAtPosition") {
        return _lookAtPositionRate.rate() / BYTES_PER_KILOBIT;
    } else if (rateName == "audioLoudness") {
        return _audioLoudnessRate.rate() / BYTES_PER_KILOBIT;
    } else if (rateName == "sensorToWorkMatrix") {
        return _sensorToWorldRate.rate() / BYTES_PER_KILOBIT;
    } else if (rateName == "additionalFlags") {
        return _additionalFlagsRate.rate() / BYTES_PER_KILOBIT;
    } else if (rateName == "parentInfo") {
        return _parentInfoRate.rate() / BYTES_PER_KILOBIT;
    } else if (rateName == "faceTracker") {
        return _faceTrackerRate.rate() / BYTES_PER_KILOBIT;
    } else if (rateName == "jointData") {
        return _jointDataRate.rate() / BYTES_PER_KILOBIT;
    } else if (rateName == "jointDefaultPoseFlagsRate") {
        return _jointDefaultPoseFlagsRate.rate() / BYTES_PER_KILOBIT;
    } else if (rateName == "farGrabJointRate") {
        return _farGrabJointRate.rate() / BYTES_PER_KILOBIT;
    } else if (rateName == "globalPositionOutbound") {
        return _outboundDataRate.globalPositionRate.rate() / BYTES_PER_KILOBIT;
    } else if (rateName == "localPositionOutbound") {
        return _outboundDataRate.localPositionRate.rate() / BYTES_PER_KILOBIT;
    } else if (rateName == "avatarBoundingBoxOutbound") {
        return _outboundDataRate.avatarBoundingBoxRate.rate() / BYTES_PER_KILOBIT;
    } else if (rateName == "avatarOrientationOutbound") {
        return _outboundDataRate.avatarOrientationRate.rate() / BYTES_PER_KILOBIT;
    } else if (rateName == "avatarScaleOutbound") {
        return _outboundDataRate.avatarScaleRate.rate() / BYTES_PER_KILOBIT;
    } else if (rateName == "lookAtPositionOutbound") {
        return _outboundDataRate.lookAtPositionRate.rate() / BYTES_PER_KILOBIT;
    } else if (rateName == "audioLoudnessOutbound") {
        return _outboundDataRate.audioLoudnessRate.rate() / BYTES_PER_KILOBIT;
    } else if (rateName == "sensorToWorkMatrixOutbound") {
        return _outboundDataRate.sensorToWorldRate.rate() / BYTES_PER_KILOBIT;
    } else if (rateName == "additionalFlagsOutbound") {
        return _outboundDataRate.additionalFlagsRate.rate() / BYTES_PER_KILOBIT;
    } else if (rateName == "parentInfoOutbound") {
        return _outboundDataRate.parentInfoRate.rate() / BYTES_PER_KILOBIT;
    } else if (rateName == "faceTrackerOutbound") {
        return _outboundDataRate.faceTrackerRate.rate() / BYTES_PER_KILOBIT;
    } else if (rateName == "jointDataOutbound") {
        return _outboundDataRate.jointDataRate.rate() / BYTES_PER_KILOBIT;
    } else if (rateName == "jointDefaultPoseFlagsOutbound") {
        return _outboundDataRate.jointDefaultPoseFlagsRate.rate() / BYTES_PER_KILOBIT;
    }
    return 0.0f;
}

template <typename Derived>
float AvatarDataStream<Derived>::getUpdateRate(const QString& rateName) const {
    if (rateName == "") {
        return _parseBufferUpdateRate.rate();
    } else if (rateName == "globalPosition") {
        return _globalPositionUpdateRate.rate();
    } else if (rateName == "localPosition") {
        return _localPositionUpdateRate.rate();
    } else if (rateName == "handControllers") {
        return _handControllersUpdateRate.rate();
    } else if (rateName == "avatarBoundingBox") {
        return _avatarBoundingBoxUpdateRate.rate();
    } else if (rateName == "avatarOrientation") {
        return _avatarOrientationUpdateRate.rate();
    } else if (rateName == "avatarScale") {
        return _avatarScaleUpdateRate.rate();
    } else if (rateName == "lookAtPosition") {
        return _lookAtPositionUpdateRate.rate();
    } else if (rateName == "audioLoudness") {
        return _audioLoudnessUpdateRate.rate();
    } else if (rateName == "sensorToWorkMatrix") {
        return _sensorToWorldUpdateRate.rate();
    } else if (rateName == "additionalFlags") {
        return _additionalFlagsUpdateRate.rate();
    } else if (rateName == "parentInfo") {
        return _parentInfoUpdateRate.rate();
    } else if (rateName == "faceTracker") {
        return _faceTrackerUpdateRate.rate();
    } else if (rateName == "jointData") {
        return _jointDataUpdateRate.rate();
    } else if (rateName == "farGrabJointData") {
        return _farGrabJointUpdateRate.rate();
    }
    return 0.0f;
}

template <typename Derived>
QVector<JointData> AvatarDataStream<Derived>::getLastSentJointData() {
    // FIXME: CRTP
    // QReadLocker readLock(&_jointDataLock);
    // _lastSentJointData.resize(_jointData.size());
    _lastSentJointData.resize(derived().getJointDataSizeOut());
    return _lastSentJointData;
}

template <typename Derived>
int AvatarDataStream<Derived>::getAverageBytesReceivedPerSecond() const {
    return lrint(_averageBytesReceived.getAverageSampleValuePerSecond());
}

template <typename Derived>
int AvatarDataStream<Derived>::getReceiveRate() const {
    return lrint(1.0f / _averageBytesReceived.getEventDeltaAverage());
}

template <typename Derived>
int AvatarDataStream<Derived>::getFauxJointIndex(const QString& name) const {
    static constexpr QChar fauxJointFirstChar('_');// The first character of all the faux joint names.
    if (!name.startsWith(fauxJointFirstChar)) {
        return -1;
    };
    if (name == "_SENSOR_TO_WORLD_MATRIX") {
        return SENSOR_TO_WORLD_MATRIX_INDEX;
    }
    if (name == "_CONTROLLER_LEFTHAND") {
        return CONTROLLER_LEFTHAND_INDEX;
    }
    if (name == "_CONTROLLER_RIGHTHAND") {
        return CONTROLLER_RIGHTHAND_INDEX;
    }
    if (name == "_CAMERA_RELATIVE_CONTROLLER_LEFTHAND") {
        return CAMERA_RELATIVE_CONTROLLER_LEFTHAND_INDEX;
    }
    if (name == "_CAMERA_RELATIVE_CONTROLLER_RIGHTHAND") {
        return CAMERA_RELATIVE_CONTROLLER_RIGHTHAND_INDEX;
    }
    if (name == "_CAMERA_MATRIX") {
        return CAMERA_MATRIX_INDEX;
    }
    if (name == "_FARGRAB_RIGHTHAND") {
        return FARGRAB_RIGHTHAND_INDEX;
    }
    if (name == "_FARGRAB_LEFTHAND") {
        return FARGRAB_LEFTHAND_INDEX;
    }
    if (name == "_FARGRAB_MOUSE") {
        return FARGRAB_MOUSE_INDEX;
    }
    return -1;
}

template <typename Derived>
int AvatarDataStream<Derived>::getJointIndex(const QString& name) const {
    int result = getFauxJointIndex(name);
    return result;
}

template <typename Derived>
void AvatarDataStream<Derived>::processAvatarIdentity(QDataStream& packetStream, bool& identityChanged,
                                       bool& displayNameChanged) {

    QUuid avatarSessionID;

    // peek the sequence number, this will tell us if we should be processing this identity packet at all
    udt::SequenceNumber::Type incomingSequenceNumberType;
    packetStream >> avatarSessionID >> incomingSequenceNumberType;
    udt::SequenceNumber incomingSequenceNumber(incomingSequenceNumberType);

    if (!_hasProcessedFirstIdentity) {
        derived().setIdentitySequenceNumberIn(incomingSequenceNumber - 1);
        // FIXME: CRTP
        // _identitySequenceNumber = incomingSequenceNumber - 1;
        _hasProcessedFirstIdentity = true;
        qCDebug(avatars) << "Processing first identity packet for" << avatarSessionID << "-"
            << (udt::SequenceNumber::Type) incomingSequenceNumber;
    }

    AvatarDataPacket::Identity identity;

    packetStream
        >> identity.attachmentData
        >> identity.displayName
        >> identity.sessionDisplayName
        >> identity.identityFlags
        ;


    const auto currentIdentitySequenceNumber = derived().getIdentitySequenceNumberOut();
    // FIXME: CRTP Derived::getIdentitySequenceNumberOut
    // return _identitySequenceNumber;
    if (incomingSequenceNumber > currentIdentitySequenceNumber) {

        // set the store identity sequence number to match the incoming identity
        derived().setIdentitySequenceNumberIn(incomingSequenceNumber);
        // FIXME: CRTP
        // _identitySequenceNumber = incomingSequenceNumber;

        auto current = derived().getIdentityDataOut();
        identityChanged = identity != current;
        displayNameChanged = identity.displayName != current.displayName;

        derived().setIdentityDataIn(std::move(identity));

        // FIXME: CRTP
        // QString displayName{};
        // bool isReplicated{};
        // bool lookAtSnappingEnabled{};
        // bool verificationFailed{};
        // QVector<AttachmentData> attachmentData;
        //
        // displayName = _displayName;
        // isReplicated = _isReplicated;
        // lookAtSnappingEnabled = _lookAtSnappingEnabled;
        // verificationFailed = _verificationFailed;
        // attachmentData = _attachmentData;
        //
        // if (identity.displayName != displayName) {
        //     _displayName = identity.displayName;
        // }
        //
        // maybeUpdateSessionDisplayNameFromTransport(identity.sessionDisplayName);
        //
        // bool flagValue;
        // flagValue = identity.identityFlags.testFlag(AvatarDataPacket::IdentityFlag::isReplicated);
        // if ( flagValue != isReplicated) {
        //     _isReplicated = flagValue;
        // }
        //
        // flagValue = identity.identityFlags.testFlag(AvatarDataPacket::IdentityFlag::lookAtSnapping);
        // if ( flagValue != lookAtSnappingEnabled) {
        //     setProperty("lookAtSnappingEnabled", flagValue);
        // }
        //
        // flagValue = identity.identityFlags.testFlag(AvatarDataPacket::IdentityFlag::verificationFailed);
        // if (flagValue != verificationFailed) {
        //     _verificationFailed = flagValue;
        //     setSkeletonModelURL(_skeletonModelURL);
        //     if (flagValue) {
        //         QString sessionDisplayName {};
        //         sessionDisplayName = getSessionDisplayName();
        //         qCDebug(avatars) << "Avatar" << sessionDisplayName << "marked as VERIFY-FAILED";
        //     }
        // };
        //
        // if (identity.attachmentData != attachmentData) {
        //     setAttachmentData(identity.attachmentData);
        // }

#ifdef WANT_DEBUG
        qCDebug(avatars) << __FUNCTION__
            << "identity.uuid:" << identity.uuid
            << "identity.displayName:" << identity.displayName
            << "identity.sessionDisplayName:" << identity.sessionDisplayName;
    } else {

        qCDebug(avatars) << "Refusing to process identity for" << uuidStringWithoutCurlyBraces(avatarSessionID) << "since"
            << (udt::SequenceNumber::Type) derived().getIdentitySequenceNumberOut()
            << "is >=" << (udt::SequenceNumber::Type) incomingSequenceNumber;
#endif
    }
}

template <typename Derived>
QByteArray AvatarDataStream<Derived>::packSkeletonData() const {
    // Send an avatar trait packet with the skeleton data before the mesh is loaded
    int avatarDataSize = 0;
    QByteArray avatarDataByteArray;

    // FIXME: CRTP
    // _avatarSkeletonDataLock.withReadLock([&] {

    auto&& avatarSkeletonData = derived().getSkeletonDataOut();

    // Add header
    AvatarSkeletonTrait::Header header;
    header.maxScaleDimension = 0.0f;
    header.maxTranslationDimension = 0.0f;
    header.numJoints = (uint8_t)avatarSkeletonData.size();
    header.stringTableLength = 0;

    for (size_t i = 0; i < avatarSkeletonData.size(); i++) {
        header.stringTableLength += (uint16_t)avatarSkeletonData[i].jointName.size();
        auto& translation = avatarSkeletonData[i].defaultTranslation;
        header.maxTranslationDimension = std::max(header.maxTranslationDimension, std::max(std::max(translation.x, translation.y), translation.z));
        header.maxScaleDimension = std::max(header.maxScaleDimension, avatarSkeletonData[i].defaultScale);
    }

    const int byteArraySize = (int)sizeof(AvatarSkeletonTrait::Header) + (int)(header.numJoints * sizeof(AvatarSkeletonTrait::JointData)) + header.stringTableLength;
    avatarDataByteArray = QByteArray(byteArraySize, 0);
    unsigned char* destinationBuffer = reinterpret_cast<unsigned char*>(avatarDataByteArray.data());
    const unsigned char* const startPosition = destinationBuffer;

    memcpy(destinationBuffer, &header, sizeof(header));
    destinationBuffer += sizeof(AvatarSkeletonTrait::Header);

    QString stringTable = "";
    for (size_t i = 0; i < avatarSkeletonData.size(); i++) {
        AvatarSkeletonTrait::JointData jdata;
        jdata.boneType = avatarSkeletonData[i].boneType;
        jdata.parentIndex = avatarSkeletonData[i].parentIndex;
        packFloatRatioToTwoByte((uint8_t*)(&jdata.defaultScale), avatarSkeletonData[i].defaultScale / header.maxScaleDimension);
        packOrientationQuatToSixBytes(jdata.defaultRotation, avatarSkeletonData[i].defaultRotation);
        packFloatVec3ToSignedTwoByteFixed(jdata.defaultTranslation, avatarSkeletonData[i].defaultTranslation / header.maxTranslationDimension, TRANSLATION_COMPRESSION_RADIX);
        jdata.jointIndex = (uint16_t)i;
        jdata.stringStart = (uint16_t)avatarSkeletonData[i].stringStart;
        jdata.stringLength = (uint8_t)avatarSkeletonData[i].stringLength;
        stringTable += avatarSkeletonData[i].jointName;
        memcpy(destinationBuffer, &jdata, sizeof(AvatarSkeletonTrait::JointData));
        destinationBuffer += sizeof(AvatarSkeletonTrait::JointData);
    }

    memcpy(destinationBuffer, stringTable.toUtf8(), header.stringTableLength);
    destinationBuffer += header.stringTableLength;

    avatarDataSize = destinationBuffer - startPosition;
    return avatarDataByteArray.left(avatarDataSize);
}

template <typename Derived>
QByteArray AvatarDataStream<Derived>::packSkeletonModelURL() const {
    return derived().getSkeletonModelURLOut();
    // FIXME: CRTP
    // return getWireSafeSkeletonModelURL().toEncoded();
}

template <typename Derived>
void AvatarDataStream<Derived>::unpackSkeletonData(const QByteArray& data) {

    const unsigned char* startPosition = reinterpret_cast<const unsigned char*>(data.data());
    const unsigned char* sourceBuffer = startPosition;

    auto header = reinterpret_cast<const AvatarSkeletonTrait::Header*>(sourceBuffer);
    sourceBuffer += sizeof(const AvatarSkeletonTrait::Header);

    std::vector<AvatarSkeletonTrait::UnpackedJointData> joints;
    for (uint8_t i = 0; i < header->numJoints; i++) {
        auto jointData = reinterpret_cast<const AvatarSkeletonTrait::JointData*>(sourceBuffer);
        sourceBuffer += sizeof(const AvatarSkeletonTrait::JointData);
        AvatarSkeletonTrait::UnpackedJointData uJointData;
        uJointData.boneType = (int)jointData->boneType;
        uJointData.jointIndex = (int)i;
        uJointData.stringLength = (int)jointData->stringLength;
        uJointData.stringStart = (int)jointData->stringStart;
        uJointData.parentIndex = ((uJointData.boneType == AvatarSkeletonTrait::BoneType::SkeletonRoot) ||
                                  (uJointData.boneType == AvatarSkeletonTrait::BoneType::NonSkeletonRoot)) ? -1 : (int)jointData->parentIndex;
        unpackOrientationQuatFromSixBytes(reinterpret_cast<const unsigned char*>(&jointData->defaultRotation), uJointData.defaultRotation);
        unpackFloatVec3FromSignedTwoByteFixed(reinterpret_cast<const unsigned char*>(&jointData->defaultTranslation), uJointData.defaultTranslation, TRANSLATION_COMPRESSION_RADIX);
        unpackFloatRatioFromTwoByte(reinterpret_cast<const unsigned char*>(&jointData->defaultScale), uJointData.defaultScale);
        uJointData.defaultTranslation *= header->maxTranslationDimension;
        uJointData.defaultScale *= header->maxScaleDimension;
        joints.push_back(uJointData);
    }
    QString table = QString::fromUtf8(reinterpret_cast<const char*>(sourceBuffer), (int)header->stringTableLength);
    for (size_t i = 0; i < joints.size(); i++) {
        QStringRef subString(&table, joints[i].stringStart, joints[i].stringLength);
        joints[i].jointName = subString.toString();
    }
    derived().setSkeletonDataIn(std::move(joints));
    // TODO: CRTP
    // if (derived().getClientTraitsHandler()) {
    //     derived().getClientTraitsHandler()->markTraitUpdated(AvatarTraits::SkeletonData);
    // }
}

template <typename Derived>
void AvatarDataStream<Derived>::unpackSkeletonModelURL(const QByteArray& data) {
    derived().setSkeletonModelURLIn(data);
    // FIXME: CRTP
    // auto skeletonModelURL = QUrl::fromEncoded(data);
    // setSkeletonModelURL(skeletonModelURL);
}

template <typename Derived>
QByteArray AvatarDataStream<Derived>::packAvatarEntityTraitInstance(AvatarTraits::TraitInstanceID traitInstanceID) {
    // grab a read lock on the avatar entities and check for entity data for the given ID
    QByteArray entityBinaryData;
    _avatarEntitiesLock.withReadLock([this, &entityBinaryData, &traitInstanceID] {
        if (_packedAvatarEntityData.contains(traitInstanceID)) {
            entityBinaryData = _packedAvatarEntityData[traitInstanceID];
        }
    });

    return entityBinaryData;
}

template <typename Derived>
QByteArray AvatarDataStream<Derived>::packGrabTraitInstance(AvatarTraits::TraitInstanceID traitInstanceID) {
    // grab a read lock on the avatar grabs and check for grab data for the given ID
    QByteArray grabBinaryData;
    _avatarGrabsLock.withReadLock([this, &grabBinaryData, &traitInstanceID] {
        if (_avatarGrabData.contains(traitInstanceID)) {
            grabBinaryData = _avatarGrabData[traitInstanceID];
        }
    });

    return grabBinaryData;
}

template <typename Derived>
QByteArray AvatarDataStream<Derived>::packTrait(AvatarTraits::TraitType traitType) const {
    QByteArray traitBinaryData;

    // Call packer function
    if (traitType == AvatarTraits::SkeletonModelURL) {
        traitBinaryData = packSkeletonModelURL();
    } else if (traitType == AvatarTraits::SkeletonData) {
        traitBinaryData = packSkeletonData();
    }

    return traitBinaryData;
}

template <typename Derived>
QByteArray AvatarDataStream<Derived>::packTraitInstance(AvatarTraits::TraitType traitType, AvatarTraits::TraitInstanceID traitInstanceID) {
    QByteArray traitBinaryData;

    // Call packer function
    if (traitType == AvatarTraits::AvatarEntity) {
        traitBinaryData = packAvatarEntityTraitInstance(traitInstanceID);
    } else if (traitType == AvatarTraits::Grab) {
        traitBinaryData = packGrabTraitInstance(traitInstanceID);
    }

    return traitBinaryData;
}

template <typename Derived>
void AvatarDataStream<Derived>::processTrait(AvatarTraits::TraitType traitType, QByteArray traitBinaryData) {
    if (traitType == AvatarTraits::SkeletonModelURL) {
        unpackSkeletonModelURL(traitBinaryData);
    } else if (traitType == AvatarTraits::SkeletonData) {
        unpackSkeletonData(traitBinaryData);
    }
}

template <typename Derived>
void AvatarDataStream<Derived>::processTraitInstance(AvatarTraits::TraitType traitType,
                                      AvatarTraits::TraitInstanceID instanceID, QByteArray traitBinaryData) {
    if (traitType == AvatarTraits::AvatarEntity) {
        storeAvatarEntityDataPayload(instanceID, traitBinaryData);
    } else if (traitType == AvatarTraits::Grab) {
        updateAvatarGrabData(instanceID, traitBinaryData);
    }
}

template <typename Derived>
void AvatarDataStream<Derived>::processDeletedTraitInstance(AvatarTraits::TraitType traitType, AvatarTraits::TraitInstanceID instanceID) {
    if (traitType == AvatarTraits::AvatarEntity) {
        clearAvatarEntityInternal(instanceID);
    } else if (traitType == AvatarTraits::Grab) {
        clearAvatarGrabData(instanceID);
    }
}

template <typename Derived>
void AvatarDataStream<Derived>::prepareResetTraitInstances() {
    if (derived().getClientTraitsHandler()) {
        _avatarEntitiesLock.withReadLock([this]{
            foreach (auto entityID, _packedAvatarEntityData.keys()) {
                derived().getClientTraitsHandler()->markInstancedTraitUpdated(AvatarTraits::AvatarEntity, entityID);
            }
            foreach (auto grabID, _avatarGrabData.keys()) {
                derived().getClientTraitsHandler()->markInstancedTraitUpdated(AvatarTraits::Grab, grabID);
            }
        });
    }
}

template <typename Derived>
QByteArray AvatarDataStream<Derived>::identityByteArray(bool setIsReplicated) const {
    QByteArray identityData;
    QDataStream identityStream(&identityData, QIODevice::Append);
    using namespace AvatarDataPacket;

    QUuid id = derived().getSessionUUIDOut();
    AvatarDataPacket::Identity identity = derived().getIdentityDataOut();

    //FIXME: CRTP to libraries/avatars/AvatarData
    // bool isReplicated{};
    // bool lookAtSnappingEnabled{};
    // bool isCertifyFailed{};
    // QVector<AttachmentData> attachmentData{};
    // QString displayName{};
    // QString sessionDisplayName{};
    //
    // isReplicated = _isReplicated;
    // lookAtSnappingEnabled = _lookAtSnappingEnabled;
    // isCertifyFailed = isCertifyFailed();
    // attachmentData = _attachmentData;
    // displayName = _displayName;
    // sessionDisplayName = getSessionDisplayNameForTransport(); // depends on _sessionDisplayName
    // IdentityFlags identityFlags = IdentityFlag::none;
    // if (isReplicated || setIsReplicated) {
    //     identityFlags.setFlag(IdentityFlag::isReplicated);
    // }
    // if (lookAtSnappingEnabled) {
    //     identityFlags.setFlag(IdentityFlag::lookAtSnapping);
    // }
    //
    // if (isCertifyFailed) {
    //     identityFlags.setFlag(IdentityFlag::verificationFailed);
    // }

    // when mixers send identity packets to agents, they simply forward along the last incoming sequence number they received
    // whereas agents send a fresh outgoing sequence number when identity data has changed

    identityStream << id
        << (udt::SequenceNumber::Type) derived().getIdentitySequenceNumberOut()
        << identity.attachmentData
        << identity.displayName
        << identity.sessionDisplayName
        << identity.identityFlags;

    return identityData;
}

// CRTP: move to derived, the client traits handler code has been separated out
// void AvatarData::setSkeletonModelURL(const QUrl& skeletonModelURL) {
//     if (skeletonModelURL.isEmpty()) {
//         qCDebug(avatars) << __FUNCTION__ << "caller called with empty URL.";
//     }
//
//     const QUrl& defaultURL = AvatarData::defaultFullAvatarModelUrl();
//
//     const QUrl& expanded = skeletonModelURL.isEmpty() ? defaultURL : skeletonModelURL;
//     if (expanded == _skeletonModelURL) {
//         return;
//     }
//
//     _skeletonModelURL = expanded;
//
//     sendSkeletonModelURL();
//
//     emit skeletonModelURLChanged
// }

template <typename Derived>
void AvatarDataStream<Derived>::sendSkeletonModelURL() {
    if (derived().getClientTraitsHandler()) {
        derived().getClientTraitsHandler()->markTraitUpdated(AvatarTraits::SkeletonModelURL);
    }
}

template <typename Derived>
int AvatarDataStream<Derived>::sendAvatarDataPacket(const QByteArray& avatarByteArray) {
    auto nodeList = DependencyManager::get<NodeList>();

    static AvatarDataSequenceNumber sequenceNumber = 0;

    auto avatarPacket = NLPacket::create(PacketType::AvatarData, avatarByteArray.size() + sizeof(sequenceNumber));
    avatarPacket->writePrimitive(sequenceNumber++);
    avatarPacket->write(avatarByteArray);
    auto packetSize = avatarPacket->getWireSize();

    nodeList->broadcastToNodes(std::move(avatarPacket), NodeSet() << NodeType::AvatarMixer);

    return packetSize;
}


template <typename Derived>
int AvatarDataStream<Derived>::sendAvatarDataPacket(AvatarDataDetail dataDetail, AvatarDataPacket::SendStatus& sendStatus) {
    // about 2% of the time, we send a full update (meaning, we transmit all the joint data), even if nothing has changed.
    // this is to guard against a joint moving once, the packet getting lost, and the joint never moving again.
    if (randFloat() < AVATAR_SEND_FULL_UPDATE_RATIO) {
        dataDetail = SendAllData;
    }

    int maxDataSize = NLPacket::maxPayloadSize(PacketType::AvatarData) - sizeof(AvatarDataSequenceNumber);

    auto data = toByteArray(AvatarDataPacket::HasFlags{}, dataDetail, getLastSentJointData(), sendStatus,
        false, // distanceAdjust
        glm::vec3{0}, // viewerPosition
        nullptr, // sentJointDataOut
        maxDataSize, // maxDataSize
        &_outboundDataRate
    );

    doneEncoding(dataDetail == CullSmallData, sendStatus);

    return sendAvatarDataPacket(data);
}

// TODO: handle immediate packet sending failures (for example when mixer is not active)
template <typename Derived>
int AvatarDataStream<Derived>::sendAllPackets(AvatarDataDetail dataDetail, AvatarDataPacket::SendStatus& sendStatus) {
    using namespace std::chrono;
    auto now = Clock::now();

    int MAX_DATA_RATE_MBPS = 3;
    int maxDataRateBytesPerSeconds = MAX_DATA_RATE_MBPS * BYTES_PER_KILOBYTE * KILO_PER_MEGA / BITS_IN_BYTE;
    int maxDataRateBytesPerMilliseconds = maxDataRateBytesPerSeconds / MSECS_PER_SECOND;

    auto bytesSent = 0;

    if (now > _nextTraitsSendWindow) {
        if (derived().getIdentityDataChanged()) {
            bytesSent += sendIdentityPacket();
        }

        if (derived().getClientTraitsHandler()) {

            if (derived().getSkeletonModelURLChanged()) {
                sendSkeletonModelURL();
            }

            if (derived().getSkeletonDataChanged()) {
                sendSkeletonData();
            }

            bytesSent += derived().getClientTraitsHandler()->sendChangedTraitsToMixer();

            derived().onClientTraitsSent();
        }

        // Compute the next send window based on how much data we sent and what
        // data rate we're trying to max at.
        milliseconds timeUntilNextSend { bytesSent / maxDataRateBytesPerMilliseconds };
        _nextTraitsSendWindow += timeUntilNextSend;

        // Don't let the next send window lag behind if we're not sending a lot of data.
        if (_nextTraitsSendWindow < now) {
            _nextTraitsSendWindow = now;
        }
    }

    bytesSent += sendAvatarDataPacket(dataDetail, sendStatus);

    return bytesSent;
}

// FIXME: CRTP move to derived
// int AvatarData::sendAvatarDataPacket(bool sendAll) {
//
//     // about 2% of the time, we send a full update (meaning, we transmit all the joint data), even if nothing has changed.
//     // this is to guard against a joint moving once, the packet getting lost, and the joint never moving again.
//
//     bool cullSmallData = !sendAll && (randFloat() < AVATAR_SEND_FULL_UPDATE_RATIO);
//     auto dataDetail = cullSmallData ? SendAllData : CullSmallData;
//
//     // NOTE: Unless sendAll is true toByteArrayStateful checks for changes (...ChangedSince() functions) based on modification timestamps compared to _lastToByteArray, which it sets to current time "afterwards" (technically it sets it beforehand, but uses the old value for the checks). This means that on subsequent toByteArrayStateful calls here this information is discarded and none of these modification are serialized for sending. It's Unclear whether this is intentional or not.
//     QByteArray avatarByteArray = toByteArrayStateful(dataDetail);
//
//     int maximumByteArraySize = NLPacket::maxPayloadSize(PacketType::AvatarData) - sizeof(AvatarDataSequenceNumber);
//
//     if (avatarByteArray.size() > maximumByteArraySize) {
//         avatarByteArray = toByteArrayStateful(dataDetail, true);
//
//         if (avatarByteArray.size() > maximumByteArraySize) {
//             avatarByteArray = toByteArrayStateful(MinimumData, true);
//
//             if (avatarByteArray.size() > maximumByteArraySize) {
//                 qCWarning(avatars) << "toByteArrayStateful() MinimumData resulted in very large buffer:" << avatarByteArray.size() << "... FAIL!!";
//                 return 0;
//             }
//         }
//     }
//
//     doneEncoding(cullSmallData);
//
//     return sendAvatarDataPacket(avatarByteArray);
// }

template <typename Derived>
int AvatarDataStream<Derived>::sendIdentityPacket() {
    auto nodeList = DependencyManager::get<NodeList>();

    if (derived().getIdentityDataChanged()) {
        // if the identity data has changed, push the sequence number forwards
        derived().pushIdentitySequenceNumber();
        // FIXME: CRTP
        // ++_identitySequenceNumber;
    }
    QByteArray identityData = identityByteArray();

    auto packetList = NLPacketList::create(PacketType::AvatarIdentity, QByteArray(), true, true);
    packetList->write(identityData);
    nodeList->eachMatchingNode(
        [](const SharedNodePointer& node)->bool {
            return node->getType() == NodeType::AvatarMixer && node->getActiveSocket();
        },
        [&](const SharedNodePointer& node) {
            nodeList->sendPacketList(std::move(packetList), *node);
    });

    derived().onIdentityDataSent();
    // FIXME: CRTP move to derived
    // _identityDataChanged = false;
    return identityData.size();
}

bool AttachmentData::operator==(const AttachmentData& other) const {
    return modelURL == other.modelURL && jointName == other.jointName && translation == other.translation &&
        rotation == other.rotation && scale == other.scale && isSoft == other.isSoft;
}

QDataStream& operator<<(QDataStream& out, const AttachmentData& attachment) {
    return out << attachment.modelURL << attachment.jointName <<
        attachment.translation << attachment.rotation << attachment.scale << attachment.isSoft;
}

QDataStream& operator>>(QDataStream& in, AttachmentData& attachment) {
    return in >> attachment.modelURL >> attachment.jointName >>
        attachment.translation >> attachment.rotation >> attachment.scale >> attachment.isSoft;
}

template <typename Derived>
void AvatarDataStream<Derived>::storeAvatarEntityDataPayload(const QUuid& entityID, const QByteArray& data) {
    bool changed = false;
    _avatarEntitiesLock.withWriteLock([&] {
        auto itr = _packedAvatarEntityData.find(entityID);
        if (itr == _packedAvatarEntityData.end()) {
            if (_packedAvatarEntityData.size() < MAX_NUM_AVATAR_ENTITIES) {
                _packedAvatarEntityData.insert(entityID, data);
                changed = true;
            }
        } else {
            itr.value() = data;
            changed = true;
        }
    });

    if (changed) {
        _avatarEntityDataChanged = true;

        if (derived().getClientTraitsHandler()) {
            // we have a client traits handler, so we need to mark this instanced trait as changed
            // so that changes will be sent next frame
            derived().getClientTraitsHandler()->markInstancedTraitUpdated(AvatarTraits::AvatarEntity, entityID);
        }
    }
}

template <typename Derived>
void AvatarDataStream<Derived>::updateAvatarEntity(const QUuid& entityID, const QByteArray& entityData) {
    // overridden where needed
    // expects 'entityData' to be a JavaScript EntityItemProperties Object in QByteArray form
}

template <typename Derived>
void AvatarDataStream<Derived>::clearAvatarEntity(const QUuid& entityID, bool requiresRemovalFromTree) {
    // NOTE: requiresRemovalFromTree is unused
    clearAvatarEntityInternal(entityID);
}

template <typename Derived>
void AvatarDataStream<Derived>::clearAvatarEntityInternal(const QUuid& entityID) {
    bool removedEntity = false;
    _avatarEntitiesLock.withWriteLock([this, &removedEntity, &entityID] {
        removedEntity = _packedAvatarEntityData.remove(entityID);
    });
    insertRemovedEntityID(entityID);
    if (removedEntity && derived().getClientTraitsHandler()) {
        // we have a client traits handler, so we need to mark this removed instance trait as deleted
        // so that changes are sent next frame
        derived().getClientTraitsHandler()->markInstancedTraitDeleted(AvatarTraits::AvatarEntity, entityID);
    }
}

template <typename Derived>
void AvatarDataStream<Derived>::clearAvatarEntities() {
    QList<QUuid> avatarEntityIDs;
    _avatarEntitiesLock.withReadLock([&] {
        avatarEntityIDs = _packedAvatarEntityData.keys();
    });
    for (const auto& entityID : avatarEntityIDs) {
        clearAvatarEntityInternal(entityID);
    }
}

template <typename Derived>
QList<QUuid> AvatarDataStream<Derived>::getAvatarEntityIDs() const {
    QList<QUuid> avatarEntityIDs;
    _avatarEntitiesLock.withReadLock([&] {
        avatarEntityIDs = _packedAvatarEntityData.keys();
    });
    return avatarEntityIDs;
}

template <typename Derived>
void AvatarDataStream<Derived>::insertRemovedEntityID(const QUuid entityID) {
    _avatarEntitiesLock.withWriteLock([&] {
        _avatarEntityRemoved.insert(entityID);
    });
    _avatarEntityDataChanged = true;
}

template <typename Derived>
AvatarEntityIDs AvatarDataStream<Derived>::getAndClearRecentlyRemovedIDs() {
    AvatarEntityIDs result;
    _avatarEntitiesLock.withWriteLock([&] {
        result = _avatarEntityRemoved;
        _avatarEntityRemoved.clear();
    });
    return result;
}

template <typename Derived>
void AvatarDataStream<Derived>::sendSkeletonData() {
    if (derived().getClientTraitsHandler()) {
        derived().getClientTraitsHandler()->markTraitUpdated(AvatarTraits::SkeletonData);
    }
}

template <typename Derived>
bool AvatarDataStream<Derived>::updateAvatarGrabData(const QUuid& grabID, const QByteArray& grabData) {
    bool changed { false };
    _avatarGrabsLock.withWriteLock([&] {
        AvatarGrabDataMap::iterator itr = _avatarGrabData.find(grabID);
        if (itr == _avatarGrabData.end()) {
            // create a new one
            if (_avatarGrabData.size() < MAX_NUM_AVATAR_GRABS) {
                _avatarGrabData.insert(grabID, grabData);
                _avatarGrabDataChanged = true;
                changed = true;
            } else {
                qCWarning(avatars) << "Can't create more grabs on avatar, limit reached.";
            }
        } else {
            // update an existing one
            if (itr.value() != grabData) {
                itr.value() = grabData;
                _avatarGrabDataChanged = true;
                changed = true;
            }
        }
    });

    return changed;
}

template <typename Derived>
void AvatarDataStream<Derived>::clearAvatarGrabData(const QUuid& grabID) {
    _avatarGrabsLock.withWriteLock([&] {
        if (_avatarGrabData.remove(grabID)) {
            _avatarGrabDataChanged = true;
        }
    });
}

#endif /* end of include guard */
