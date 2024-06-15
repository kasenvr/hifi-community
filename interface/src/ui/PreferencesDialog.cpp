//
//  Re-created Bradley Austin Davis on 2016/01/22
//  Copyright 2016 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#include "PreferencesDialog.h"

#include <AudioClient.h>
#include <avatar/AvatarManager.h>
#include <ScriptEngines.h>
#include <OffscreenUi.h>
#include <Preferences.h>
#include <plugins/PluginUtils.h>
#include <display-plugins/CompositorHelper.h>
#include <display-plugins/hmd/HmdDisplayPlugin.h>
#include "scripting/RenderScriptingInterface.h"
#include "Application.h"
#include "DialogsManager.h"
#include "LODManager.h"
#include "Menu.h"
#include "Snapshot.h"
#include "SnapshotAnimated.h"
#include "UserActivityLogger.h"
#include "ui/Keyboard.h"

void setupPreferences() {
    auto preferences = DependencyManager::get<Preferences>();
    auto myAvatar = DependencyManager::get<AvatarManager>()->getMyAvatar();
    static const QString AVATAR_BASICS { "Avatar Basics" };
    {
        auto getter = [myAvatar]()->QString { return myAvatar->getDisplayName(); };
        auto setter = [myAvatar](const QString& value) { myAvatar->setDisplayName(value); };
        auto preference = new EditPreference(AVATAR_BASICS, "Avatar display name (optional)", getter, setter);
        preference->setPlaceholderText("Not showing a name");
        preferences->addPreference(preference);
    }

    {
        auto getter = [myAvatar]()->QString { return myAvatar->getCollisionSoundURL(); };
        auto setter = [myAvatar](const QString& value) { myAvatar->setCollisionSoundURL(value); };
        auto preference = new EditPreference(AVATAR_BASICS, "Avatar collision sound URL (optional)", getter, setter);
        preference->setPlaceholderText("Enter the URL of a sound to play when you bump into something");
        preferences->addPreference(preference);
    }

    {
        auto getter = [myAvatar]()->QString { return myAvatar->getFullAvatarURLFromPreferences().toString(); };
        auto setter = [myAvatar](const QString& value) { myAvatar->useFullAvatarURL(value, ""); qApp->clearAvatarOverrideUrl(); };
        auto preference = new AvatarPreference(AVATAR_BASICS, "Appearance", getter, setter);
        preferences->addPreference(preference);
    }

    // Graphics quality
    static const QString GRAPHICS_QUALITY { "Graphics Quality" };
    {
        auto getter = []()->float {
            return (int)DependencyManager::get<LODManager>()->getWorldDetailQuality();
        };

        auto setter = [](int value) {
            DependencyManager::get<LODManager>()->setWorldDetailQuality(static_cast<WorldDetailQuality>(value));
        };

        auto wodButtons = new RadioButtonsPreference(GRAPHICS_QUALITY, "World Detail", getter, setter);
        QStringList items;
        items << "Low World Detail" << "Medium World Detail" << "High World Detail";
        wodButtons->setHeading("World Detail");
        wodButtons->setItems(items);
        preferences->addPreference(wodButtons);

        auto getterShadow = []()->bool {
            auto menu = Menu::getInstance();
            return menu->isOptionChecked(MenuOption::Shadows);
        };
        auto setterShadow = [](bool value) {
            auto menu = Menu::getInstance();
            menu->setIsOptionChecked(MenuOption::Shadows, value);
        };
        preferences->addPreference(new CheckPreference(GRAPHICS_QUALITY, "Show Shadows", getterShadow, setterShadow));
    }

    {
        auto getter = []()->QString {
            RefreshRateManager::RefreshRateProfile refreshRateProfile = qApp->getRefreshRateManager().getRefreshRateProfile();
            return QString::fromStdString(RefreshRateManager::refreshRateProfileToString(refreshRateProfile));
        };

        auto setter = [](QString value) {
            std::string profileName = value.toStdString();
            RefreshRateManager::RefreshRateProfile refreshRateProfile = RefreshRateManager::refreshRateProfileFromString(profileName);
            qApp->getRefreshRateManager().setRefreshRateProfile(refreshRateProfile);
        };

        auto preference = new ComboBoxPreference(GRAPHICS_QUALITY, "Refresh Rate", getter, setter);
        QStringList refreshRateProfiles
            { QString::fromStdString(RefreshRateManager::refreshRateProfileToString(RefreshRateManager::RefreshRateProfile::ECO)),
              QString::fromStdString(RefreshRateManager::refreshRateProfileToString(RefreshRateManager::RefreshRateProfile::INTERACTIVE)),
              QString::fromStdString(RefreshRateManager::refreshRateProfileToString(RefreshRateManager::RefreshRateProfile::REALTIME)) };

        preference->setItems(refreshRateProfiles);
        preferences->addPreference(preference);

        auto getterMaterialProceduralShaders = []() -> bool {
            auto menu = Menu::getInstance();
            return menu->isOptionChecked(MenuOption::MaterialProceduralShaders);
        };
        auto setterMaterialProceduralShaders = [](bool value) {
            auto menu = Menu::getInstance();
            menu->setIsOptionChecked(MenuOption::MaterialProceduralShaders, value);
        };
        preferences->addPreference(new CheckPreference(GRAPHICS_QUALITY, "Enable Procedural Materials", getterMaterialProceduralShaders, setterMaterialProceduralShaders));
    }
    {
        // Expose the Viewport Resolution Scale
        auto getter = []()->float {
            return RenderScriptingInterface::getInstance()->getViewportResolutionScale();
        };

        auto setter = [](float value) {
            RenderScriptingInterface::getInstance()->setViewportResolutionScale(value);
        };

        auto scaleSlider = new SliderPreference(GRAPHICS_QUALITY, "Resolution Scale", getter, setter);
        scaleSlider->setMin(0.25f);
        scaleSlider->setMax(1.0f);
        scaleSlider->setStep(0.02f);
        preferences->addPreference(scaleSlider);
    }

    // UI
    static const QString UI_CATEGORY { "User Interface" };
    {
        auto getter = []()->bool { return qApp->getSettingConstrainToolbarPosition(); };
        auto setter = [](bool value) { qApp->setSettingConstrainToolbarPosition(value); };
        preferences->addPreference(new CheckPreference(UI_CATEGORY, "Constrain Toolbar Position to Horizontal Center", getter, setter));
    }

    {
        auto getter = []()->bool { return qApp->getAwayStateWhenFocusLostInVREnabled(); };
        auto setter = [](bool value) { qApp->setAwayStateWhenFocusLostInVREnabled(value); };
        preferences->addPreference(new CheckPreference(UI_CATEGORY, "Go into away state when interface window loses focus in VR", getter, setter));
    }

    {
        auto getter = []()->float { return qApp->getDesktopTabletScale(); };
        auto setter = [](float value) { qApp->setDesktopTabletScale(value); };
        auto preference = new SpinnerPreference(UI_CATEGORY, "Desktop Tablet Scale %", getter, setter);
        preference->setMin(20);
        preference->setMax(500);
        preferences->addPreference(preference);
    }

    {
        auto getter = []()->float { return qApp->getHMDTabletScale(); };
        auto setter = [](float value) { qApp->setHMDTabletScale(value); };
        auto preference = new SpinnerPreference(UI_CATEGORY, "VR Tablet Scale %", getter, setter);
        preference->setMin(20);
        preference->setMax(500);
        preferences->addPreference(preference);
    }

    {
        static const QString RETICLE_ICON_NAME = { Cursor::Manager::getIconName(Cursor::Icon::RETICLE) };
        auto getter = []()->bool { return qApp->getPreferredCursor() == RETICLE_ICON_NAME; };
        auto setter = [](bool value) { qApp->setPreferredCursor(value ? RETICLE_ICON_NAME : QString()); };
        preferences->addPreference(new CheckPreference(UI_CATEGORY, "Use reticle cursor instead of arrow", getter, setter));
    }

    {
        auto getter = []()->bool { return qApp->getMiniTabletEnabled(); };
        auto setter = [](bool value) { qApp->setMiniTabletEnabled(value); };
        preferences->addPreference(new CheckPreference(UI_CATEGORY, "Use mini tablet", getter, setter));
    }

    {
        auto getter = []()->int { return DependencyManager::get<Keyboard>()->getUse3DKeyboard(); };
        auto setter = [](int value) { DependencyManager::get<Keyboard>()->setUse3DKeyboard(value); };
        preferences->addPreference(new CheckPreference(UI_CATEGORY, "Use Virtual Keyboard", getter, setter));
    }

    {
        auto getter = []()->bool { return DependencyManager::get<Keyboard>()->getPreferMalletsOverLasers() ? 1 : 0; };
        auto setter = [](bool value) { return DependencyManager::get<Keyboard>()->setPreferMalletsOverLasers((bool)value); };
        auto preference = new RadioButtonsPreference(UI_CATEGORY, "Keyboard laser / mallets", getter, setter);
        QStringList items;
        items << "Lasers" << "Mallets";
        preference->setItems(items);
        preference->setIndented(true);
        preferences->addPreference(preference);
    }


    {
        auto getter = []()->int { return qApp->getPreferStylusOverLaser() ? 1 : 0; };
        auto setter = [](int value) { qApp->setPreferStylusOverLaser((bool)value); };
        auto preference = new RadioButtonsPreference(UI_CATEGORY, "Tablet stylys / laser", getter, setter);
        QStringList items;
        items << "Lasers" << "Stylus";
        preference->setHeading("Tablet Input Mechanism");
        preference->setItems(items);
        preferences->addPreference(preference);
    }

    static const QString VIEW_CATEGORY{ "View" };
    {
        auto getter = [myAvatar]()->float { return myAvatar->getRealWorldFieldOfView(); };
        auto setter = [myAvatar](float value) { myAvatar->setRealWorldFieldOfView(value); };
        auto preference = new SpinnerPreference(VIEW_CATEGORY, "Real world vertical field of view (angular size of monitor)", getter, setter);
        preference->setMin(1);
        preference->setMax(180);
        preferences->addPreference(preference);
    }
    {
        auto getter = []()->float { return qApp->getFieldOfView(); };
        auto setter = [](float value) { qApp->setFieldOfView(value); };
        auto preference = new SpinnerPreference(VIEW_CATEGORY, "Vertical field of view", getter, setter);
        preference->setMin(1);
        preference->setMax(180);
        preference->setStep(1);
        preferences->addPreference(preference);
    }

    {
        auto getter = []()->bool { return qApp->getPreferAvatarFingerOverStylus(); };
        auto setter = [](bool value) { qApp->setPreferAvatarFingerOverStylus(value); };
        preferences->addPreference(new CheckPreference(UI_CATEGORY, "Prefer Avatar Finger Over Stylus", getter, setter));
    }

    // Snapshots
    static const QString SNAPSHOTS { "Snapshots" };
    {
        auto getter = []()->QString { return DependencyManager::get<Snapshot>()->_snapshotsLocation.get(); };
        auto setter = [](const QString& value) { DependencyManager::get<Snapshot>()->_snapshotsLocation.set(value); emit DependencyManager::get<Snapshot>()->snapshotLocationSet(value); };
        auto preference = new BrowsePreference(SNAPSHOTS, "Put my snapshots here", getter, setter);
        preferences->addPreference(preference);
    }
    {
        auto getter = []()->float { return SnapshotAnimated::snapshotAnimatedDuration.get(); };
        auto setter = [](float value) { SnapshotAnimated::snapshotAnimatedDuration.set(value); };
        auto preference = new SpinnerPreference(SNAPSHOTS, "Animated Snapshot Duration", getter, setter);
        preference->setMin(1);
        preference->setMax(5);
        preference->setStep(1);
        preferences->addPreference(preference);
    }

    {
        auto getter = []()->bool { return !Menu::getInstance()->isOptionChecked(MenuOption::DisableActivityLogger); };
        auto setter = [](bool value) { Menu::getInstance()->setIsOptionChecked(MenuOption::DisableActivityLogger, !value); };
        preferences->addPreference(new CheckPreference("Privacy", "Send data - Vircadia uses information provided by your "
                                "client to improve the product through the logging of errors, tracking of usage patterns, "
                                "installation and system details. By allowing Vircadia to collect this information "
                                "you are helping to improve the product. ", getter, setter));
    }

    {
        auto getter = []()->bool { return Menu::getInstance()->isOptionChecked(MenuOption::GenerateAndSubmitCrashReports); };
        auto setter = [](bool value) { Menu::getInstance()->setIsOptionChecked(MenuOption::GenerateAndSubmitCrashReports, !value); };
        auto preference = new CheckPreference("Privacy", "Send crashes - Vircadia can use information provided by your "
                                "client to improve the product through crash reports. By allowing developers to collect "
                                "this information you are helping to improve the product. ", getter, setter);
        if (!UserActivityLogger::getInstance().isCrashMonitorEnabled()) {
            preference->setFunctionalityDisabledTooltip("Your version of the application was not built with support for crash reporting.");
        }
        preferences->addPreference(preference);
    }

    static const QString AVATAR_TUNING { "Avatar Tuning" };
    {
        auto getter = [myAvatar]()->QString { return myAvatar->getDominantHand(); };
        auto setter = [myAvatar](const QString& value) { myAvatar->setDominantHand(value); };
        preferences->addPreference(new PrimaryHandPreference(AVATAR_TUNING, "Dominant Hand", getter, setter));
    }
    {
        auto getter = [myAvatar]()->float { return myAvatar->getTargetScale(); };
        auto setter = [myAvatar](float value) { myAvatar->setTargetScale(value); };
        auto preference = new SpinnerSliderPreference(AVATAR_TUNING, "Avatar Scale", getter, setter);
        preference->setMin(0.25);
        preference->setMax(4);
        preference->setStep(0.05f);
        preference->setDecimals(2);
        preferences->addPreference(preference);

        // When the Interface is first loaded, this section setupPreferences(); is loaded -
        // causing the myAvatar->getDomainMinScale() and myAvatar->getDomainMaxScale() to get set to incorrect values
        // which can't be changed across domain switches. Having these values loaded up when you load the Dialog each time
        // is a way around this, therefore they're not specified here but in the QML.
    }

    {
        auto getter = [myAvatar]()->QString { return myAvatar->getAnimGraphOverrideUrl().toString(); };
        auto setter = [myAvatar](const QString& value) { myAvatar->setAnimGraphOverrideUrl(QUrl(value)); };
        auto preference = new EditPreference(AVATAR_TUNING, "Avatar animation JSON", getter, setter);
        preference->setPlaceholderText("default");
        preferences->addPreference(preference);
    }

    {
        auto getter = [myAvatar]()->bool { return myAvatar->getCollisionsEnabled(); };
        auto setter = [myAvatar](bool value) { myAvatar->setCollisionsEnabled(value); };
        auto preference = new CheckPreference(AVATAR_TUNING, "Enable Avatar collisions", getter, setter);
        preferences->addPreference(preference);
    }

    static const QString VR_MOVEMENT{ "VR Movement" };
    {
        auto getter = [myAvatar]()->bool { return myAvatar->getAllowTeleporting(); };
        auto setter = [myAvatar](bool value) { myAvatar->setAllowTeleporting(value); };
        auto preference = new CheckPreference(VR_MOVEMENT, "Teleporting", getter, setter);
        preferences->addPreference(preference);
    }
    {
        auto getter = [myAvatar]()->bool { return myAvatar->useAdvancedMovementControls(); };
        auto setter = [myAvatar](bool value) { myAvatar->setUseAdvancedMovementControls(value); };
        auto preference = new CheckPreference(VR_MOVEMENT, "Walking", getter, setter);
        preferences->addPreference(preference);
    }
    {
        auto getter = [myAvatar]()->bool { return myAvatar->getStrafeEnabled(); };
        auto setter = [myAvatar](bool value) { myAvatar->setStrafeEnabled(value); };
        preferences->addPreference(new CheckPreference(VR_MOVEMENT, "Strafing", getter, setter));
    }
    {
        auto getter = [myAvatar]()->bool { return myAvatar->getFlyingHMDPref(); };
        auto setter = [myAvatar](bool value) { myAvatar->setFlyingHMDPref(value); };
        auto preference = new CheckPreference(VR_MOVEMENT, "Jumping and flying", getter, setter);
        preference->setIndented(true);
        preferences->addPreference(preference);
    }
    {
        auto getter = [myAvatar]()->bool { return myAvatar->hoverWhenUnsupported(); };
        auto setter = [myAvatar](bool value) { myAvatar->setHoverWhenUnsupported(value); };
        auto preference = new CheckPreference(VR_MOVEMENT, "Hover When Unsupported", getter, setter);
        preferences->addPreference(preference);
    }
    {
        auto getter = [myAvatar]()->int { return myAvatar->getMovementReference(); };
        auto setter = [myAvatar](int value) { myAvatar->setMovementReference(value); };
        //auto preference = new CheckPreference(VR_MOVEMENT, "Hand-Relative Movement", getter, setter);
        auto preference = new RadioButtonsPreference(VR_MOVEMENT, "Movement Direction", getter, setter);
        QStringList items;
        items << "HMD-Relative" << "Hand-Relative" << "Hand-Relative (Leveled)";
        preference->setHeading("Movement Direction");
        preference->setItems(items);
        preferences->addPreference(preference);
    }
    {
        auto getter = [myAvatar]()->QString { return myAvatar->getDominantHand(); };
        auto setter = [myAvatar](const QString& value) { myAvatar->setDominantHand(value); };
        preferences->addPreference(new PrimaryHandPreference(VR_MOVEMENT, "Dominant Hand", getter, setter));
    }
    {
        auto getter = [myAvatar]()->int { return myAvatar->getSnapTurn() ? 0 : 1; };
        auto setter = [myAvatar](int value) { myAvatar->setSnapTurn(value == 0); };
        auto preference = new RadioButtonsPreference(VR_MOVEMENT, "Snap turn / Smooth turn", getter, setter);
        QStringList items;
        items << "Snap turn" << "Smooth turn";
        preference->setHeading("Rotation mode");
        preference->setItems(items);
        preferences->addPreference(preference);
    }
    {
        auto getter = [myAvatar]()->float { return qApp->getCamera().getSensitivity(); };
        auto setter = [myAvatar](float value) { qApp->getCamera().setSensitivity(value); };
        auto preference = new SpinnerSliderPreference(VR_MOVEMENT, "Camera Sensitivity", getter, setter);
        preference->setMin(0.1f);
        preference->setMax(5.0f);
        preference->setStep(0.1f);
        preference->setDecimals(1);
        preferences->addPreference(preference);
    }
    {
        auto getter = [myAvatar]()->int { return myAvatar->getControlScheme(); };
        auto setter = [myAvatar](int index) { myAvatar->setControlScheme(index); };
        auto preference = new RadioButtonsPreference(VR_MOVEMENT, "Control Scheme", getter, setter);
        QStringList items;
        items << "Default" << "Analog" << "Analog++";
        preference->setHeading("Control Scheme Selection");
        preference->setItems(items);
        preferences->addPreference(preference);
    }
    {
        auto getter = [myAvatar]()->float { return myAvatar->getAnalogPlusWalkSpeed(); };
        auto setter = [myAvatar](float value) { myAvatar->setAnalogPlusWalkSpeed(value); };
        auto preference = new SpinnerSliderPreference(VR_MOVEMENT, "Analog++ Walk Speed", getter, setter);
        preference->setMin(6.0f);
        preference->setMax(30.0f);
        preference->setStep(1);
        preference->setDecimals(0);
        preferences->addPreference(preference);
    }
    {
        auto getter = []()->bool {
            return qApp->getVisionSqueeze().getVisionSqueezeEnabled();
        };
        auto setter = [](bool value) {
            qApp->getVisionSqueeze().setVisionSqueezeEnabled(value);
        };
        auto preference = new CheckPreference(VR_MOVEMENT, "Enable HMD Comfort Mode", getter, setter);
        preferences->addPreference(preference);
    }
    {
        const float sliderPositions = 5.0f;
        auto getter = [sliderPositions]()->float {
            return roundf(sliderPositions * qApp->getVisionSqueeze().getVisionSqueezeRatioX());
        };
        auto setter = [sliderPositions](float value) {
            float ratio = value / sliderPositions;
            qApp->getVisionSqueeze().setVisionSqueezeRatioX(ratio);
            qApp->getVisionSqueeze().setVisionSqueezeRatioY(ratio);
        };
        auto preference = new SpinnerSliderPreference(VR_MOVEMENT, "Comfort Mode", getter, setter);
        preference->setMin(0.0f);
        preference->setMax(sliderPositions);
        preference->setStep(1.0f);
        preference->setDecimals(0);
        preferences->addPreference(preference);
    }
    {
        auto getter = [myAvatar]()->bool { return myAvatar->getShowPlayArea(); };
        auto setter = [myAvatar](bool value) { myAvatar->setShowPlayArea(value); };
        auto preference = new CheckPreference(VR_MOVEMENT, "Show room boundaries while teleporting", getter, setter);
        preferences->addPreference(preference);
    }
    {
        IntPreference::Getter getter = [myAvatar]() -> int {
            return static_cast<int>(myAvatar->getAllowAvatarStandingPreference());
        };

        IntPreference::Setter setter = [myAvatar](const int& value) {
            myAvatar->setAllowAvatarStandingPreference(static_cast<MyAvatar::AllowAvatarStandingPreference>(value));
        };

        auto preference = new RadioButtonsPreference(VR_MOVEMENT, "Allow my avatar to stand", getter, setter);
        QStringList items;
        items << "When I'm standing"
              << "Always";  // Must match the order in MyAvatar::AllowAvatarStandingPreference.
        assert(items.size() == static_cast<uint>(MyAvatar::AllowAvatarStandingPreference::Count));
        preference->setHeading("Allow my avatar to stand:");
        preference->setItems(items);
        preferences->addPreference(preference);
    }
    {
        IntPreference::Getter getter = [myAvatar]() -> int {
            return static_cast<int>(myAvatar->getAllowAvatarLeaningPreference());
        };

        IntPreference::Setter setter = [myAvatar](const int& value) {
            myAvatar->setAllowAvatarLeaningPreference(static_cast<MyAvatar::AllowAvatarLeaningPreference>(value));
        };

        auto preference = new RadioButtonsPreference(VR_MOVEMENT, "Allow my avatar to lean", getter, setter);
        QStringList items;
        items << "When I'm standing"
              << "Always"
              << "Never"
              << "Always, no recenter (Experimental)";  // Must match the order in MyAvatar::AllowAvatarLeaningPreference.
        assert(items.size() == static_cast<uint>(MyAvatar::AllowAvatarLeaningPreference::Count));
        preference->setHeading("Allow my avatar to lean:");
        preference->setItems(items);
        preferences->addPreference(preference);
    }
    {
        auto getter = [=]()->float { return myAvatar->getUserHeight(); };
        auto setter = [=](float value) { myAvatar->setUserHeight(value); };
        auto preference = new SpinnerPreference(VR_MOVEMENT, "User real-world height (meters)", getter, setter);
        preference->setMin(1.0f);
        preference->setMax(2.2f);
        preference->setDecimals(2);
        preference->setStep(0.01f);
        preferences->addPreference(preference);
    }

    static const QString AVATAR_CAMERA{ "Mouse Sensitivity" };
    {
        auto getter = [myAvatar]()->float { return myAvatar->getPitchSpeed(); };
        auto setter = [myAvatar](float value) { myAvatar->setPitchSpeed(value); };
        auto preference = new SpinnerSliderPreference(AVATAR_CAMERA, "Y input:", getter, setter);
        preference->setMin(1.0f);
        preference->setMax(360.0f);
        preference->setStep(1);
        preference->setDecimals(0);
        preferences->addPreference(preference);
    }
    {
        auto getter = [myAvatar]()->float { return myAvatar->getYawSpeed(); };
        auto setter = [myAvatar](float value) { myAvatar->setYawSpeed(value); };
        auto preference = new SpinnerSliderPreference(AVATAR_CAMERA, "X input:", getter, setter);
        preference->setMin(1.0f);
        preference->setMax(360.0f);
        preference->setStep(1);
        preference->setDecimals(0);
        preferences->addPreference(preference);
    }

    static const QString AUDIO_BUFFERS("Audio Buffers");
    {
        auto getter = []()->bool { return !DependencyManager::get<AudioClient>()->getReceivedAudioStream().dynamicJitterBufferEnabled(); };
        auto setter = [](bool value) { DependencyManager::get<AudioClient>()->getReceivedAudioStream().setDynamicJitterBufferEnabled(!value); };
        auto preference = new CheckPreference(AUDIO_BUFFERS, "Disable dynamic jitter buffer", getter, setter);
        preferences->addPreference(preference);
    }
    {
        auto getter = []()->float { return DependencyManager::get<AudioClient>()->getReceivedAudioStream().getStaticJitterBufferFrames(); };
        auto setter = [](float value) { DependencyManager::get<AudioClient>()->getReceivedAudioStream().setStaticJitterBufferFrames(value); };
        auto preference = new SpinnerPreference(AUDIO_BUFFERS, "Static jitter buffer frames", getter, setter);
        preference->setMin(0);
        preference->setMax(2000);
        preference->setStep(1);
        preferences->addPreference(preference);
    }
    {
        auto getter = []()->bool { return !DependencyManager::get<AudioClient>()->getOutputStarveDetectionEnabled(); };
        auto setter = [](bool value) { DependencyManager::get<AudioClient>()->setOutputStarveDetectionEnabled(!value); };
        auto preference = new CheckPreference(AUDIO_BUFFERS, "Disable output starve detection", getter, setter);
        preferences->addPreference(preference);
    }
    {
        auto getter = []()->float { return DependencyManager::get<AudioClient>()->getOutputBufferSize(); };
        auto setter = [](float value) { DependencyManager::get<AudioClient>()->setOutputBufferSize(value); };
        auto preference = new SpinnerPreference(AUDIO_BUFFERS, "Output buffer initial frames", getter, setter);
        preference->setMin(AudioClient::MIN_BUFFER_FRAMES);
        preference->setMax(AudioClient::MAX_BUFFER_FRAMES);
        preference->setStep(1);
        preferences->addPreference(preference);
    }
#if DEV_BUILD || PR_BUILD
    {
        auto getter = []()->bool { return DependencyManager::get<AudioClient>()->isSimulatingJitter(); };
        auto setter = [](bool value) { return DependencyManager::get<AudioClient>()->setIsSimulatingJitter(value); };
        auto preference = new CheckPreference(AUDIO_BUFFERS, "Packet jitter simulator", getter, setter);
        preferences->addPreference(preference);
    }
    {
        auto getter = []()->float { return DependencyManager::get<AudioClient>()->getGateThreshold(); };
        auto setter = [](float value) { return DependencyManager::get<AudioClient>()->setGateThreshold(value); };
        auto preference = new SpinnerPreference(AUDIO_BUFFERS, "Packet throttle threshold", getter, setter);
        preference->setMin(1);
        preference->setMax(200);
        preference->setStep(1);
        preferences->addPreference(preference);
    }
#endif

    {
        static const QString NETWORKING("Networking");

        QWeakPointer<NodeList> nodeListWeak = DependencyManager::get<NodeList>();
        {
            static const int MIN_PORT_NUMBER { 0 };
            static const int MAX_PORT_NUMBER { 65535 };
            auto getter = [nodeListWeak] {
                auto nodeList = nodeListWeak.lock();
                if (nodeList) {
                    return static_cast<int>(nodeList->getSocketLocalPort(SocketType::UDP));
                } else {
                    return -1;
                }
            };
            auto setter = [nodeListWeak](int preset) {
                auto nodeList = nodeListWeak.lock();
                if (nodeList) {
                    nodeList->setSocketLocalPort(SocketType::UDP, static_cast<quint16>(preset));
                }
            };
            auto preference = new IntSpinnerPreference(NETWORKING, "Listening Port", getter, setter);
            preference->setMin(MIN_PORT_NUMBER);
            preference->setMax(MAX_PORT_NUMBER);
            preferences->addPreference(preference);
        }

        {
            auto getter = []()->float { return qApp->getMaxOctreePacketsPerSecond(); };
            auto setter = [](float value) { qApp->setMaxOctreePacketsPerSecond(value); };
            auto preference = new SpinnerPreference(NETWORKING, "Max entities packets sent each second", getter, setter);
            preference->setMin(60);
            preference->setMax(6000);
            preference->setStep(10);
            preferences->addPreference(preference);
        }
    }
}
