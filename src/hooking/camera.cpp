#include "cemu_hooks.h"
#include "rendering/openxr.h"
#include "instance.h"

#include <glm/glm.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/gtc/matrix_access.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/projection.hpp>
#include <glm/gtx/quaternion.hpp>
#include <glm/gtc/constants.hpp>

OpenXR::EyeSide CemuHooks::s_eyeSide = OpenXR::EyeSide::LEFT;

//    // Calculate center view
//    // todo: can we really create a center view because each eye is getting updated separately?
//    // todo: I think we should use one eye's view for ANY calculations and keep the other eye for the next frame
//    XrView leftView = VRManager::instance().XR->GetPredictedView(OpenXR::EyeSide::LEFT);
//    XrView rightView = VRManager::instance().XR->GetPredictedView(OpenXR::EyeSide::RIGHT);
//    glm::fvec3 leftEyePos(leftView.pose.position.x, leftView.pose.position.y, leftView.pose.position.z);
//    glm::fvec3 rightEyePos(rightView.pose.position.x, rightView.pose.position.y, rightView.pose.position.z);
//    glm::fvec3 hmdPos = glm::mix(leftEyePos, rightEyePos, 0.5f);
// fixme: VRManager::instance().XR->UpdatePoses(m_eyeSide);

void CemuHooks::hook_UpdateCamera(PPCInterpreter_t* hCPU) {
    //Log::print("Updated camera!");
    hCPU->instructionPointer = hCPU->gpr[7];

    // Read the camera matrix from the game's memory
    uint32_t ppc_cameraMatrixOffsetIn = hCPU->gpr[30];
    data_VRCameraIn origCameraMatrix = {};

    readMemory(ppc_cameraMatrixOffsetIn, &origCameraMatrix);
    swapEndianness(origCameraMatrix.posX);
    swapEndianness(origCameraMatrix.posY);
    swapEndianness(origCameraMatrix.posZ);
    swapEndianness(origCameraMatrix.targetX);
    swapEndianness(origCameraMatrix.targetY);
    swapEndianness(origCameraMatrix.targetZ);
    swapEndianness(origCameraMatrix.fov);

//    // todo: Calculate new view directions
//    glm::fquat combinedQuat = glm::normalize(lookAtQuat);
//    glm::fmat3 combinedMatrix = glm::toMat3(combinedQuat);
//
//    glm::fvec3 rotatedHmdPos = combinedQuat * currEyePos;
//
//    // Convert the calculated parameters into the new camera matrix provided by the game
//    float newTargetX = oldPosition.x + ((combinedMatrix[2][0] * -1.0f) * originalCameraDistance) + rotatedHmdPos.x;
//    float newTargetY = oldPosition.y + ((combinedMatrix[2][1] * -1.0f) * originalCameraDistance) + rotatedHmdPos.y + settings.heightPositionOffsetSetting;
//    float newTargetZ = oldPosition.z + ((combinedMatrix[2][2] * -1.0f) * originalCameraDistance) + rotatedHmdPos.z;
//
//    float newPosX = oldPosition.x + rotatedHmdPos.x;
//    float newPosY = oldPosition.y + rotatedHmdPos.y + settings.heightPositionOffsetSetting;
//    float newPosZ = oldPosition.z + rotatedHmdPos.z;
//
//    float newRotX = combinedMatrix[1][0];
//    float newRotY = combinedMatrix[1][1];
//    float newRotZ = combinedMatrix[1][2];
//
//    float newFOV = origCameraMatrix.fov;
//    float newAspectRatio = 0.872665;

    // Current VR headset camera matrix
    XrView currView = VRManager::instance().XR->GetPredictedView(s_eyeSide);
    glm::fvec3 currEyePos(currView.pose.position.x, currView.pose.position.y, currView.pose.position.z);
    glm::fquat currEyeQuat(currView.pose.orientation.w, currView.pose.orientation.x, currView.pose.orientation.y, currView.pose.orientation.z);
    Log::print("Headset View: x={}, y={}, z={}, orientW={}, orientX={}, orientY={}, orientZ={}", currEyePos.x, currEyePos.y, currEyePos.z, currEyeQuat.w, currEyeQuat.x, currEyeQuat.y, currEyeQuat.z);

    // Current in-game camera matrix
    glm::fvec3 oldCameraPosition(origCameraMatrix.posX, origCameraMatrix.posY, origCameraMatrix.posZ);
    glm::fvec3 oldCameraTarget(origCameraMatrix.targetX, origCameraMatrix.targetY, origCameraMatrix.targetZ);
    float originalCameraDistance = glm::distance(oldCameraPosition, oldCameraTarget);
    Log::print("Original Game Camera: x={}, y={}, z={}, targetX={}, targetY={}, targetZ={}", oldCameraPosition.x, oldCameraPosition.y, oldCameraPosition.z, oldCameraTarget.x, oldCameraTarget.y, oldCameraTarget.z);
//    oldCameraPosition.x = -768.26385f;
//    oldCameraPosition.y = 191.32312f;
//    oldCameraPosition.z = 1781.4119f;
//    oldCameraTarget.x = -764.00354f;
//    oldCameraTarget.y = 191.32312f;
//    oldCameraTarget.z = 1784.7822f;

    glm::fvec3 forwardVector = oldCameraTarget - oldCameraPosition;
    forwardVector = glm::normalize(forwardVector);
    glm::fquat lookAtQuat = glm::quatLookAtRH(forwardVector, {0.0, 1.0, 0.0});
    glm::fquat hmdQuat = glm::fquat(currEyeQuat.w, currEyeQuat.x, currEyeQuat.y, currEyeQuat.z);

    glm::fquat combinedQuat = lookAtQuat * hmdQuat;
    glm::fmat3 combinedMatrix = glm::toMat3(hmdQuat);

    glm::fvec3 rotatedHmdPos = combinedQuat * currEyePos;

    float newTargetX = oldCameraPosition.x + ((combinedMatrix[2][0] * -1.0f) * originalCameraDistance) + rotatedHmdPos.x;
    float newTargetY = oldCameraPosition.y + ((combinedMatrix[2][1] * -1.0f) * originalCameraDistance) + rotatedHmdPos.y;
    float newTargetZ = oldCameraPosition.z + ((combinedMatrix[2][2] * -1.0f) * originalCameraDistance) + rotatedHmdPos.z;

    float newPosX = oldCameraPosition.x + rotatedHmdPos.x;
    float newPosY = oldCameraPosition.y + rotatedHmdPos.y + 1.0f;
    float newPosZ = oldCameraPosition.z + rotatedHmdPos.z;

    float newRotX = combinedMatrix[1][0]; // 0.128136
    float newRotY = combinedMatrix[1][1]; // 0.981351
    float newRotZ = combinedMatrix[1][2]; // -0.143287

    float newFOV = origCameraMatrix.fov; // Assuming symmetric vertical and horizontal FOV
    float newAspectRatio = 0.872665f;

    Log::print("New Game Camera: x={}, y={}, z={}, targetX={}, targetY={}, targetZ={}, rotX={}, rotY={}, rotZ={}", newPosX, newPosY, newPosZ, newTargetX, newTargetY, newTargetZ, newRotX, newRotY, newRotZ);

    // Write the camera matrix to the game's memory
    uint32_t ppc_cameraMatrixOffsetOut = hCPU->gpr[31];
    data_VRCameraOut updatedCameraMatrix = {
        .posX = newPosX,
        .posY = newPosY,
        .posZ = newPosZ,
        .targetX = newTargetX,
        .targetY = newTargetY,
        .targetZ = newTargetZ,
        .rotX = newRotX,
        .rotY = newRotY,
        .rotZ = newRotZ,
        .fov = newFOV,
        .aspectRatio = newAspectRatio
    };

    swapEndianness(updatedCameraMatrix.posX);
    swapEndianness(updatedCameraMatrix.posY);
    swapEndianness(updatedCameraMatrix.posZ);
    swapEndianness(updatedCameraMatrix.targetX);
    swapEndianness(updatedCameraMatrix.targetY);
    swapEndianness(updatedCameraMatrix.targetZ);
    swapEndianness(updatedCameraMatrix.rotX);
    swapEndianness(updatedCameraMatrix.rotY);
    swapEndianness(updatedCameraMatrix.rotZ);
    swapEndianness(updatedCameraMatrix.fov);
    swapEndianness(updatedCameraMatrix.aspectRatio);
    writeMemory(ppc_cameraMatrixOffsetOut, updatedCameraMatrix);
}