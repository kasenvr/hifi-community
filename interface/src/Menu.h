//
//  Menu.h
//  interface/src
//
//  Created by Stephen Birarda on 8/12/13.
//  Copyright 2013 High Fidelity, Inc.
//  Copyright 2020 Vircadia contributors.
//  Copyright 2020 DigiSomni LLC.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#include <ui/Menu.h>

#ifndef hifi_Menu_h
#define hifi_Menu_h

class MenuItemProperties;

class Menu : public ui::Menu {
    Q_OBJECT

public:
    static Menu* getInstance();
    Menu();
    Q_INVOKABLE void addMenuItem(const MenuItemProperties& properties);
};

namespace MenuOption {
    const QString AddRemoveFriends = "Add/Remove Friends...";
    const QString AddressBar = "Show Address Bar";
    const QString Animations = "Animations...";
    const QString AnimDebugDrawAnimPose = "Debug Draw Animation";
    const QString AnimDebugDrawBaseOfSupport = "Debug Draw Base of Support";
    const QString AnimDebugDrawDefaultPose = "Debug Draw Default Pose";
    const QString AnimDebugDrawPosition= "Debug Draw Position";
    const QString AnimDebugDrawOtherSkeletons = "Debug Draw Other Skeletons";
    const QString AskToResetSettings = "Ask To Reset Settings on Start";
    const QString AssetMigration = "ATP Asset Migration";
    const QString AssetServer = "Asset Browser";
    const QString AudioScope = "Show Scope";
    const QString AudioScopeFiftyFrames = "Fifty";
    const QString AudioScopeFiveFrames = "Five";
    const QString AudioScopeFrames = "Display Frames";
    const QString AudioScopePause = "Pause Scope";
    const QString AudioScopeTwentyFrames = "Twenty";
    const QString AudioStatsShowInjectedStreams = "Audio Stats Show Injected Streams";
    const QString AutoMuteAudio = "Auto Mute Microphone";
    const QString AvatarReceiveStats = "Show Receive Stats";
    const QString AvatarBookmarks = "Avatar Bookmarks";
    const QString AvatarPackager = "Avatar Packager";
    const QString Back = "Back";
    const QString BinaryEyelidControl = "Binary Eyelid Control";
    const QString BookmarkAvatar = "Bookmark Avatar";
    const QString BookmarkAvatarEntities = "Bookmark Avatar Entities";
    const QString BookmarkLocation = "Bookmark Location";
    const QString CalibrateCamera = "Calibrate Camera";
    const QString CachebustRequire = "Enable Cachebusting of Script.require";
    const QString CenterPlayerInView = "Center Player In View";
    const QString Chat = "Chat...";
    const QString ClearDiskCaches = "Clear Disk Caches (requires restart)";
    const QString Collisions = "Collisions";
    const QString Connexion = "Activate 3D Connexion Devices";
    const QString Console = "Console...";
    const QString ControlWithSpeech = "Enable Speech Control API";
    const QString CopyAddress = "Copy Address to Clipboard";
    const QString CopyPath = "Copy Path to Clipboard";
    const QString CoupleEyelids = "Couple Eyelids";
    const QString CrashPureVirtualFunction = "Pure Virtual Function Call";
    const QString CrashPureVirtualFunctionThreaded = "Pure Virtual Function Call (threaded)";
    const QString CrashDoubleFree = "Double Free";
    const QString CrashDoubleFreeThreaded = "Double Free (threaded)";
    const QString CrashNullDereference = "Null Dereference";
    const QString CrashNullDereferenceThreaded = "Null Dereference (threaded)";
    const QString CrashAbort = "Abort";
    const QString CrashAbortThreaded = "Abort (threaded)";
    const QString CrashOnShutdown = "Crash During Shutdown";
    const QString CrashOutOfBoundsVectorAccess = "Out of Bounds Vector Access";
    const QString CrashOutOfBoundsVectorAccessThreaded = "Out of Bounds Vector Access (threaded)";
    const QString CrashNewFault = "New Fault";
    const QString CrashNewFaultThreaded = "New Fault (threaded)";
    const QString CrashThrownException = "Thrown C++ exception";
    const QString CrashThrownExceptionThreaded = "Thrown C++ exception (threaded)";
    const QString CreateEntitiesGrabbable = "Create Entities As Grabbable (except Zones, Particles, and Lights)";
    const QString DeadlockInterface = "Deadlock Interface";
    const QString UnresponsiveInterface = "Unresponsive Interface";
    const QString DecreaseAvatarSize = "Decrease Avatar Size";
    const QString DefaultSkybox = "Default Skybox";
    const QString DeleteAvatarBookmark = "Delete Avatar Bookmark...";
    const QString DeleteAvatarEntitiesBookmark = "Delete Avatar Entities Bookmark";
    const QString DeleteBookmark = "Delete Bookmark...";
    const QString DisableActivityLogger = "Disable Activity Logger";
    const QString DisableEyelidAdjustment = "Disable Eyelid Adjustment";
    const QString DisableLightEntities = "Disable Light Entities";
    const QString DisplayCrashOptions = "Display Crash Options";
    const QString DisplayHandTargets = "Show Hand Targets";
    const QString DisplayModelBounds = "Display Model Bounds";
    const QString DisplayModelTriangles = "Display Model Triangles";
    const QString DisplayModelElementChildProxies = "Display Model Element Children";
    const QString DisplayModelElementProxy = "Display Model Element Bounds";
    const QString DisplayDebugTimingDetails = "Display Timing Details";
    const QString LocationBookmarks = "Bookmarks";
    const QString DontDoPrecisionPicking = "Don't Do Precision Picking";
    const QString DontRenderEntitiesAsScene = "Don't Render Entities as Scene";
    const QString EchoLocalAudio = "Echo Local Audio";
    const QString EchoServerAudio = "Echo Server Audio";
    const QString EnableFlying = "Enable Flying";
    const QString EnableAvatarCollisions = "Enable Avatar Collisions";
    const QString EnableInverseKinematics = "Enable Inverse Kinematics";
    const QString EntityScriptServerLog = "Entity Script Server Log";
    const QString ExpandMyAvatarSimulateTiming = "Expand /myAvatar/simulation";
    const QString ExpandMyAvatarTiming = "Expand /myAvatar";
    const QString ExpandOtherAvatarTiming = "Expand /otherAvatar";
    const QString ExpandPaintGLTiming = "Expand /paintGL";
    const QString ExpandSimulationTiming = "Expand /simulation";
    const QString ExpandPhysicsTiming = "Expand /physics";
    const QString ExpandUpdateTiming = "Expand /update";
    const QString FirstPerson = "First Person Legacy";
    const QString FirstPersonLookAt = "First Person";
    const QString FirstPersonHMD = "Enter First Person Mode in HMD";
    const QString FivePointCalibration = "5 Point Calibration";
    const QString FixGaze = "Fix Gaze (no saccade)";
    const QString Forward = "Forward";
    const QString FrameTimer = "Show Timer";
    const QString GenerateAndSubmitCrashReports = "Generate and Submit Crash Reports";
    const QString Help = "Help...";
    const QString HomeLocation = "Home ";
    const QString IncreaseAvatarSize = "Increase Avatar Size";
    const QString ActionMotorControl = "Enable Default Motor Control";
    const QString LastLocation = "Last Location";
    const QString LoadScript = "Open and Run Script File...";
    const QString LoadScriptURL = "Open and Run Script from URL...";
    const QString LodTools = "LOD Tools";
    const QString Login = "Login/Sign Up";
    const QString Log = "Log";
    const QString LogExtraTimings = "Log Extra Timing Details";
    const QString LookAtCamera = "Third Person";
    const QString LowVelocityFilter = "Low Velocity Filter";
    const QString MeshVisible = "Draw Mesh";
    const QString MuteEnvironment = "Mute Environment";
    const QString MuteFaceTracking = "Mute Face Tracking";
    const QString NamesAboveHeads = "Names Above Heads";
    const QString Networking = "Networking...";
    const QString NoFaceTracking = "None";
    const QString OctreeStats = "Entity Statistics";
    const QString OnePointCalibration = "1 Point Calibration";
    const QString OnlyDisplayTopTen = "Only Display Top Ten";
    const QString OpenVrThreadedSubmit = "OpenVR Threaded Submit"; 
    const QString OutputMenu = "Display";
    const QString Overlays = "Show Overlays";
    const QString PackageModel = "Package Avatar as .fst...";
    const QString Pair = "Pair";
    const QString PhysicsShowOwned = "Highlight Simulation Ownership";
    const QString VerboseLogging = "Verbose Logging";
    const QString PhysicsShowBulletWireframe = "Show Bullet Collision";
    const QString PhysicsShowBulletAABBs = "Show Bullet Bounding Boxes";
    const QString PhysicsShowBulletContactPoints = "Show Bullet Contact Points";
    const QString PhysicsShowBulletConstraints = "Show Bullet Constraints";
    const QString PhysicsShowBulletConstraintLimits = "Show Bullet Constraint Limits";
    const QString PipelineWarnings = "Log Render Pipeline Warnings";
    const QString Preferences = "General...";
    const QString Quit =  "Quit";
    const QString ReloadAllScripts = "Reload All Scripts";
    const QString ReloadContent = "Reload Content (Clears all caches)";
    const QString RenderClearKtxCache = "Clear KTX Cache (requires restart)";
    const QString RenderMaxTextureMemory = "Maximum Texture Memory";
    const QString RenderMaxTextureAutomatic = "Automatic Texture Memory";
    const QString RenderMaxTexture4MB = "4 MB";
    const QString RenderMaxTexture64MB = "64 MB";
    const QString RenderMaxTexture256MB = "256 MB";
    const QString RenderMaxTexture512MB = "512 MB";
    const QString RenderMaxTexture1024MB = "1024 MB";
    const QString RenderMaxTexture2048MB = "2048 MB";
    const QString RenderMaxTexture3072MB = "3072 MB";
    const QString RenderMaxTexture4096MB = "4096 MB";
    const QString RenderMaxTexture6144MB = "6144 MB";
    const QString RenderMaxTexture8192MB = "8192 MB";
    const QString RenderSensorToWorldMatrix = "Show SensorToWorld Matrix";
    const QString RenderIKTargets = "Show IK Targets";
    const QString RenderIKConstraints = "Show IK Constraints";
    const QString RenderIKChains = "Show IK Chains";
    const QString RenderDetailedCollision = "Show Detailed Collision";
    const QString ResetAvatarSize = "Reset Avatar Size";
    const QString ResetSensors = "Reset Sensors";
    const QString RunningScripts = "Running Scripts...";
    const QString RunTimingTests = "Run Timing Tests";
    const QString ScriptedMotorControl = "Enable Scripted Motor Control";
    const QString EntityScriptQMLWhitelist = "Entity Script / QML Whitelist";
    const QString ShowTrackedObjects = "Show Tracked Objects";
    const QString SelfieCamera = "Selfie";
    const QString SendWrongDSConnectVersion = "Send wrong DS connect version";
    const QString SendWrongProtocolVersion = "Send wrong protocol version";
    const QString SetHomeLocation = "Set Home Location";
    const QString ShowBordersEntityNodes = "Show Entity Nodes";
    const QString ShowBoundingCollisionShapes = "Show Bounding Collision Shapes";
    const QString ShowDSConnectTable = "Show Domain Connection Timing";
    const QString ShowMyLookAtVectors = "Show My Eye Vectors";
    const QString ShowMyLookAtTarget = "Show My Look-At Target";
    const QString ShowOtherLookAtVectors = "Show Other Eye Vectors";
    const QString ShowOtherLookAtTarget = "Show Other Look-At Target";
    const QString EnableLookAtSnapping = "Enable LookAt Snapping";
    const QString ShowRealtimeEntityStats = "Show Realtime Entity Stats";
    const QString SimulateEyeTracking = "Simulate";
    const QString SMIEyeTracking = "SMI Eye Tracking";
    const QString SparseTextureManagement = "Enable Sparse Texture Management";
    const QString StartUpLocation = "Start-Up Location";
    const QString Stats = "Show Statistics";
    const QString AnimStats = "Show Animation Stats";
    const QString StopAllScripts = "Stop All Scripts";
    const QString SuppressShortTimings = "Suppress Timings Less than 10ms";
    const QString ThirdPerson = "Third Person Legacy";
    const QString ThreePointCalibration = "3 Point Calibration";
    const QString ThrottleFPSIfNotFocus = "Throttle FPS If Not Focus"; // FIXME - this value duplicated in Basic2DWindowOpenGLDisplayPlugin.cpp
    const QString ToolWindow = "Tool Window";
    const QString TransmitterDrive = "Transmitter Drive";
    const QString TurnWithHead = "Turn using Head";
    const QString UseAudioForMouth = "Use Audio for Mouth";
    const QString UseCamera = "Use Camera";
    const QString VelocityFilter = "Velocity Filter";
    const QString VisibleToEveryone = "Everyone";
    const QString VisibleToFriends = "Friends";
    const QString VisibleToNoOne = "No one";
    const QString WorldAxes = "World Axes";
    const QString DesktopTabletToToolbar = "Desktop Tablet Becomes Toolbar";
    const QString HMDTabletToToolbar = "HMD Tablet Becomes Toolbar";
    const QString Shadows = "Shadows";
    const QString AmbientOcclusion = "Ambient Occlusion";
    const QString NotificationSounds = "play_notification_sounds";
    const QString NotificationSoundsSnapshot = "play_notification_sounds_snapshot";
    const QString NotificationSoundsTablet = "play_notification_sounds_tablet";
    const QString ForceCoarsePicking = "Force Coarse Picking";
    const QString ComputeBlendshapes = "Compute Blendshapes";
    const QString HighlightTransitions = "Highlight Transitions";
    const QString MaterialProceduralShaders = "Enable Procedural Materials";
    const QString GreenBoxesForMissingModels = "Green Boxes For Missing Models";
}

#endif // hifi_Menu_h

