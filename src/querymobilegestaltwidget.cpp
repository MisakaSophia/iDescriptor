/*
 * iDescriptor: A free and open-source idevice management tool.
 *
 * Copyright (C) 2025 Uncore <https://github.com/uncor3>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published
 * by the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */

#include "querymobilegestaltwidget.h"
#include <QApplication>
#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QTimer>
#include <sstream>

QueryMobileGestaltWidget::QueryMobileGestaltWidget(iDescriptorDevice *device,
                                                   QWidget *parent)
    : QWidget(parent), m_device(device)
{
    // FIXME: not tested on iOS 17,18 but it's deprecated on iOS 26
    // i am assuming it won't work
    if (m_device->deviceInfo.parsedDeviceVersion.major > 16) {
        QMessageBox::warning(this, "Unsupported iOS Version",
                             "Apple deprecated this protocol for Devices "
                             "running iOS 17 or later");
        QTimer::singleShot(0, this, &QWidget::close);
        return;
    }
    setupUI();
    populateKeys();
}

void QueryMobileGestaltWidget::setupUI()
{
    setWindowTitle("Query MobileGestalt - iDescriptor");
    setMinimumSize(800, 600);

    // Main layout
    mainLayout = new QVBoxLayout(this);

    // Title
    QLabel *desc = new QLabel("This tool lets you query MobileGestalt keys , "
                              "which provide various device information.");
    desc->setStyleSheet("margin:5px;");
    mainLayout->addWidget(desc);

    // Selection group
    selectionGroup = new QGroupBox("Select MobileGestalt Keys");
    mainLayout->addWidget(selectionGroup);

    QVBoxLayout *groupLayout = new QVBoxLayout(selectionGroup);
    groupLayout->setContentsMargins(0, 0, 0, 0);

    // Select/Clear buttons
    buttonLayout = new QHBoxLayout();
    selectAllButton = new QPushButton("Select All");
    clearAllButton = new QPushButton("Clear All");
    selectAllButton->setMaximumWidth(100);
    clearAllButton->setMaximumWidth(100);
    buttonLayout->addWidget(selectAllButton);
    buttonLayout->addWidget(clearAllButton);
    buttonLayout->addStretch();
    buttonLayout->setContentsMargins(5, 0, 5, 0);
    groupLayout->addLayout(buttonLayout);

    // Scroll area for checkboxes
    scrollArea = new QScrollArea();
    scrollArea->setWidgetResizable(true);
    scrollArea->setMaximumHeight(200);
    scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    checkboxWidget = new QWidget();
    checkboxLayout = new QVBoxLayout(checkboxWidget);
    checkboxLayout->setContentsMargins(10, 5, 10, 5);

    scrollArea->setWidget(checkboxWidget);
    groupLayout->addWidget(scrollArea);

    // Query button
    queryButton = new QPushButton("Query MobileGestalt");
    queryButton->setProperty("primary", true);
    queryButton->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    mainLayout->addWidget(queryButton, 0, Qt::AlignCenter);

    // Status label
    statusLabel = new QLabel("Select keys and click Query to begin");
    statusLabel->setStyleSheet("color: #666; font-style: italic; margin: 5px;");
    mainLayout->addWidget(statusLabel);

    QGroupBox *outputGroup = new QGroupBox("Query Results");
    outputTextEdit = new QTextEdit();
    outputTextEdit->setReadOnly(true);
    outputTextEdit->setPlaceholderText("results will appear here...");
    outputTextEdit->setStyleSheet("QTextEdit {"
                                  "border : none;"
                                  "}");
    outputGroup->setLayout(new QVBoxLayout());
    outputGroup->layout()->setContentsMargins(0, 0, 0, 0);
    outputGroup->layout()->addWidget(outputTextEdit);
    mainLayout->addWidget(outputGroup);

    // Connect signals
    connect(queryButton, &QPushButton::clicked, this,
            &QueryMobileGestaltWidget::onQueryButtonClicked);
    connect(selectAllButton, &QPushButton::clicked, this,
            &QueryMobileGestaltWidget::onSelectAllClicked);
    connect(clearAllButton, &QPushButton::clicked, this,
            &QueryMobileGestaltWidget::onClearAllClicked);
}

void QueryMobileGestaltWidget::populateKeys()
{
    // Credits ->
    // https://github.com/doronz88/pymobiledevice3/blob/master/pymobiledevice3/services/diagnostics.py
    mobileGestaltKeys = {
        "3GProximityCapability",
        "3GVeniceCapability",
        "3Gvenice",
        "3d-imagery",
        "3d-maps",
        "64-bit",
        "720p",
        "720pPlaybackCapability",
        "APNCapability",
        "ARM64ExecutionCapability",
        "ARMV6ExecutionCapability",
        "ARMV7ExecutionCapability",
        "ARMV7SExecutionCapability",
        "ASTC",
        "AWDID",
        "AWDLCapability",
        "AccelerometerCapability",
        "AccessibilityCapability",
        "AcousticID",
        "ActivationProtocol",
        "ActiveWirelessTechnology",
        "ActuatorResonantFrequency",
        "AdditionalTextTonesCapability",
        "AggregateDevicePhotoZoomFactor",
        "AggregateDeviceVideoZoomFactor",
        "AirDropCapability",
        "AirDropRestriction",
        "AirplaneMode",
        "AirplayMirroringCapability",
        "AllDeviceCapabilities",
        "Allow32BitApps",
        "AllowOnlyATVCPSDKApps",
        "AllowYouTube",
        "AllowYouTubePlugin",
        "AmbientLightSensorCapability",
        "AmbientLightSensorSerialNumber",
        "ApNonce",
        "ApNonceRetrieve",
        "AppCapacityTVOS",
        "AppStore",
        "AppStoreCapability",
        "AppleInternalInstallCapability",
        "AppleNeuralEngineSubtype",
        "ApplicationInstallationCapability",
        "ArcModuleSerialNumber",
        "ArrowChipID",
        "ArrowUniqueChipID",
        "ArtworkTraits",
        "AssistantCapability",
        "AudioPlaybackCapability",
        "AutoFocusCameraCapability",
        "AvailableDisplayZoomSizes",
        "BacklightCapability",
        "BasebandAPTimeSync",
        "BasebandBoardSnum",
        "BasebandCertId",
        "BasebandChipId",
        "BasebandChipset",
        "BasebandClass",
        "BasebandFirmwareManifestData",
        "BasebandFirmwareUpdateInfo",
        "BasebandFirmwareVersion",
        "BasebandKeyHashInformation",
        "BasebandPostponementStatus",
        "BasebandPostponementStatusBlob",
        "BasebandRegionSKU",
        "BasebandRegionSKURadioTechnology",
        "BasebandSecurityInfoBlob",
        "BasebandSerialNumber",
        "BasebandSkeyId",
        "BasebandStatus",
        "BasebandUniqueId",
        "BatteryCurrentCapacity",
        "BatteryIsCharging",
        "BatteryIsFullyCharged",
        "BatterySerialNumber",
        "BlueLightReductionSupported",
        "BluetoothAddress",
        "BluetoothAddressData",
        "BluetoothCapability",
        "BluetoothLE2Capability",
        "BluetoothLECapability",
        "BoardId",
        "BoardRevision",
        "BootManifestHash",
        "BootNonce",
        "BridgeBuild",
        "BridgeRestoreVersion",
        "BuddyLanguagesAnimationRequiresOptimization",
        "BuildID",
        "BuildVersion",
        "C2KDeviceCapability",
        "CPUArchitecture",
        "CPUSubType",
        "CPUType",
        "CallForwardingCapability",
        "CallWaitingCapability",
        "CallerIDCapability",
        "CameraAppUIVersion",
        "CameraCapability",
        "CameraFlashCapability",
        "CameraFrontFlashCapability",
        "CameraHDR2Capability",
        "CameraHDRVersion",
        "CameraLiveEffectsCapability",
        "CameraMaxBurstLength",
        "CameraRestriction",
        "CarrierBundleInfoArray",
        "CarrierInstallCapability",
        "CellBroadcastCapability",
        "CellularDataCapability",
        "CellularTelephonyCapability",
        "CertificateProductionStatus",
        "CertificateSecurityMode",
        "ChipID",
        "CloudPhotoLibraryCapability",
        "CoastlineGlowRenderingCapability",
        "CompassCalibration",
        "CompassCalibrationDictionary",
        "CompassType",
        "ComputerName",
        "ConferenceCallType",
        "ConfigNumber",
        "ContainsCellularRadioCapability",
        "ContinuityCapability",
        "CoreRoutineCapability",
        "CoverglassSerialNumber",
        "DMin",
        "DataPlanCapability",
        "DebugBoardRevision",
        "DelaySleepForHeadsetClickCapability",
        "DesenseBuild",
        "DeviceAlwaysPrewarmActuator",
        "DeviceBackGlassMaterial",
        "DeviceBackingColor",
        "DeviceBrand",
        "DeviceClass",
        "DeviceClassNumber",
        "DeviceColor",
        "DeviceColorMapPolicy",
        "DeviceCornerRadius",
        "DeviceCoverGlassColor",
        "DeviceCoverGlassMaterial",
        "DeviceCoverMaterial",
        "DeviceEnclosureColor",
        "DeviceEnclosureMaterial",
        "DeviceEnclosureRGBColor",
        "DeviceHasAggregateCamera",
        "DeviceHousingColor",
        "DeviceIsMuseCapable",
        "DeviceKeyboardCalibration",
        "DeviceLaunchTimeLimitScale",
        "DeviceName",
        "DeviceNameString",
        "DevicePrefers3DBuildingStrokes",
        "DevicePrefersBuildingStrokes",
        "DevicePrefersCheapTrafficShaders",
        "DevicePrefersProceduralAntiAliasing",
        "DevicePrefersTrafficAlpha",
        "DeviceProximityCapability",
        "DeviceRGBColor",
        "DeviceRequiresPetalOptimization",
        "DeviceRequiresProximityAmeliorations",
        "DeviceRequiresSoftwareBrightnessCalculations",
        "DeviceSceneUpdateTimeLimitScale",
        "DeviceSubBrand",
        "DeviceSupports1080p",
        "DeviceSupports3DImagery",
        "DeviceSupports3DMaps",
        "DeviceSupports3rdPartyHaptics",
        "DeviceSupports4G",
        "DeviceSupports4k",
        "DeviceSupports64Bit",
        "DeviceSupports720p",
        "DeviceSupports9Pin",
        "DeviceSupportsAOP",
        "DeviceSupportsARKit",
        "DeviceSupportsASTC",
        "DeviceSupportsAdaptiveMapsUI",
        "DeviceSupportsAlwaysListening",
        "DeviceSupportsAlwaysOnCompass",
        "DeviceSupportsAlwaysOnTime",
        "DeviceSupportsApplePencil",
        "DeviceSupportsAutoLowLightVideo",
        "DeviceSupportsAvatars",
        "DeviceSupportsBatteryModuleAuthentication",
        "DeviceSupportsBerkelium2",
        "DeviceSupportsCCK",
        "DeviceSupportsCameraCaptureOnTouchDown",
        "DeviceSupportsCameraDeferredProcessing",
        "DeviceSupportsCameraHaptics",
        "DeviceSupportsCarIntegration",
        "DeviceSupportsCinnamon",
        "DeviceSupportsClosedLoopHaptics",
        "DeviceSupportsCrudeProx",
        "DeviceSupportsDClr",
        "DeviceSupportsDoNotDisturbWhileDriving",
        "DeviceSupportsELabel",
        "DeviceSupportsEnhancedAC3",
        "DeviceSupportsEnvironmentalDosimetry",
        "DeviceSupportsExternalHDR",
        "DeviceSupportsFloorCounting",
        "DeviceSupportsHDRDeferredProcessing",
        "DeviceSupportsHMEInARKit",
        "DeviceSupportsHaptics",
        "DeviceSupportsHardwareDetents",
        "DeviceSupportsHeartHealthAlerts",
        "DeviceSupportsHeartRateVariability",
        "DeviceSupportsHiResBuildings",
        "DeviceSupportsLineIn",
        "DeviceSupportsLiquidDetection_CorrosionMitigation",
        "DeviceSupportsLivePhotoAuto",
        "DeviceSupportsLongFormAudio",
        "DeviceSupportsMapsBlurredUI",
        "DeviceSupportsMapsOpticalHeading",
        "DeviceSupportsMomentCapture",
        "DeviceSupportsNFC",
        "DeviceSupportsNavigation",
        "DeviceSupportsNewton",
        "DeviceSupportsOnDemandPhotoAnalysis",
        "DeviceSupportsP3ColorspaceVideoRecording",
        "DeviceSupportsPeriodicALSUpdates",
        "DeviceSupportsPhotosLocalLight",
        "DeviceSupportsPortraitIntensityAdjustments",
        "DeviceSupportsPortraitLightEffectFilters",
        "DeviceSupportsRGB10",
        "DeviceSupportsRaiseToSpeak",
        "DeviceSupportsSiDP",
        "DeviceSupportsSideButtonClickSpeed",
        "DeviceSupportsSimplisticRoadMesh",
        "DeviceSupportsSingleCameraPortrait",
        "DeviceSupportsSiriBargeIn",
        "DeviceSupportsSiriSpeaks",
        "DeviceSupportsSiriSpokenMessages",
        "DeviceSupportsSpatialOverCapture",
        "DeviceSupportsStereoAudioRecording",
        "DeviceSupportsStudioLightPortraitPreview",
        "DeviceSupportsSwimmingWorkouts",
        "DeviceSupportsTapToWake",
        "DeviceSupportsTelephonyOverUSB",
        "DeviceSupportsTethering",
        "DeviceSupportsToneMapping",
        "DeviceSupportsUSBTypeC",
        "DeviceSupportsVSHCompensation",
        "DeviceSupportsVoiceOverCanUseSiriVoice",
        "DeviceSupportsWebkit",
        "DeviceSupportsWirelessSplitting",
        "DeviceSupportsYCbCr10",
        "DeviceVariant",
        "DeviceVariantGuess",
        "DiagData",
        "DictationCapability",
        "DieId",
        "DiskUsage",
        "DisplayDriverICChipID",
        "DisplayFCCLogosViaSoftwareCapability",
        "DisplayMirroringCapability",
        "DisplayPortCapability",
        "DualSIMActivationPolicyCapable",
        "EUICCChipID",
        "EffectiveProductionStatus",
        "EffectiveProductionStatusAp",
        "EffectiveProductionStatusSEP",
        "EffectiveSecurityMode",
        "EffectiveSecurityModeAp",
        "EffectiveSecurityModeSEP",
        "EncodeAACCapability",
        "EncryptedDataPartitionCapability",
        "EnforceCameraShutterClick",
        "EnforceGoogleMail",
        "EthernetMacAddress",
        "EthernetMacAddressData",
        "ExplicitContentRestriction",
        "ExternalChargeCapability",
        "ExternalPowerSourceConnected",
        "FDRSealingStatus",
        "FMFAllowed",
        "FaceTimeBackCameraTemporalNoiseReductionMode",
        "FaceTimeBitRate2G",
        "FaceTimeBitRate3G",
        "FaceTimeBitRateLTE",
        "FaceTimeBitRateWiFi",
        "FaceTimeCameraRequiresFastSwitchOptions",
        "FaceTimeCameraSupportsHardwareFaceDetection",
        "FaceTimeDecodings",
        "FaceTimeEncodings",
        "FaceTimeFrontCameraTemporalNoiseReductionMode",
        "FaceTimePhotosOptIn",
        "FaceTimePreferredDecoding",
        "FaceTimePreferredEncoding",
        "FirmwareNonce",
        "FirmwarePreflightInfo",
        "FirmwareVersion",
        "FirstPartyLaunchTimeLimitScale",
        "ForwardCameraCapability",
        "FrontCameraOffsetFromDisplayCenter",
        "FrontCameraRotationFromDisplayNormal",
        "FrontFacingCameraAutoHDRCapability",
        "FrontFacingCameraBurstCapability",
        "FrontFacingCameraCapability",
        "FrontFacingCameraHDRCapability",
        "FrontFacingCameraHDROnCapability",
        "FrontFacingCameraHFRCapability",
        "FrontFacingCameraHFRVideoCapture1080pMaxFPS",
        "FrontFacingCameraHFRVideoCapture720pMaxFPS",
        "FrontFacingCameraMaxVideoZoomFactor",
        "FrontFacingCameraModuleSerialNumber",
        "FrontFacingCameraStillDurationForBurst",
        "FrontFacingCameraVideoCapture1080pMaxFPS",
        "FrontFacingCameraVideoCapture4kMaxFPS",
        "FrontFacingCameraVideoCapture720pMaxFPS",
        "FrontFacingIRCameraModuleSerialNumber",
        "FrontFacingIRStructuredLightProjectorModuleSerialNumber",
        "Full6FeaturesCapability",
        "GPSCapability",
        "GSDeviceName",
        "GameKitCapability",
        "GasGaugeBatteryCapability",
        "GreenTeaDeviceCapability",
        "GyroscopeCapability",
        "H264EncoderCapability",
        "HDRImageCaptureCapability",
        "HDVideoCaptureCapability",
        "HEVCDecoder10bitSupported",
        "HEVCDecoder12bitSupported",
        "HEVCDecoder8bitSupported",
        "HEVCEncodingCapability",
        "HMERefreshRateInARKit",
        "HWModelStr",
        "HallEffectSensorCapability",
        "HardwareEncodeSnapshotsCapability",
        "HardwareKeyboardCapability",
        "HardwarePlatform",
        "HardwareSnapshotsRequirePurpleGfxCapability",
        "HasAllFeaturesCapability",
        "HasAppleNeuralEngine",
        "HasBaseband",
        "HasBattery",
        "HasDaliMode",
        "HasExtendedColorDisplay",
        "HasIcefall",
        "HasInternalSettingsBundle",
        "HasMesa",
        "HasPKA",
        "HasSEP",
        "HasSpringBoard",
        "HasThinBezel",
        "HealthKitCapability",
        "HearingAidAudioEqualizationCapability",
        "HearingAidLowEnergyAudioCapability",
        "HearingAidPowerReductionCapability",
        "HiDPICapability",
        "HiccoughInterval",
        "HideNonDefaultApplicationsCapability",
        "HighestSupportedVideoMode",
        "HomeButtonType",
        "HomeScreenWallpaperCapability",
        "IDAMCapability",
        "IOSurfaceBackedImagesCapability",
        "IOSurfaceFormatDictionary",
        "IceFallID",
        "IcefallInRestrictedMode",
        "IcefallInfo",
        "Image4CryptoHashMethod",
        "Image4Supported",
        "InDiagnosticsMode",
        "IntegratedCircuitCardIdentifier",
        "IntegratedCircuitCardIdentifier2",
        "InternalBuild",
        "InternationalMobileEquipmentIdentity",
        "InternationalMobileEquipmentIdentity2",
        "InternationalSettingsCapability",
        "InverseDeviceID",
        "IsEmulatedDevice",
        "IsLargeFormatPhone",
        "IsPwrOpposedVol",
        "IsServicePart",
        "IsSimulator",
        "IsThereEnoughBatteryLevelForSoftwareUpdate",
        "IsUIBuild",
        "JasperSerialNumber",
        "LTEDeviceCapability",
        "LaunchTimeLimitScaleSupported",
        "LisaCapability",
        "LoadThumbnailsWhileScrollingCapability",
        "LocalizedDeviceNameString",
        "LocationRemindersCapability",
        "LocationServicesCapability",
        "LowPowerWalletMode",
        "LunaFlexSerialNumber",
        "LynxPublicKey",
        "LynxSerialNumber",
        "MLBSerialNumber",
        "MLEHW",
        "MMSCapability",
        "MacBridgingKeys",
        "MagnetometerCapability",
        "MainDisplayRotation",
        "MainScreenCanvasSizes",
        "MainScreenClass",
        "MainScreenHeight",
        "MainScreenOrientation",
        "MainScreenPitch",
        "MainScreenScale",
        "MainScreenStaticInfo",
        "MainScreenWidth",
        "MarketingNameString",
        "MarketingProductName",
        "MarketingVersion",
        "MaxH264PlaybackLevel",
        "MaximumScreenScale",
        "MedusaFloatingLiveAppCapability",
        "MedusaOverlayAppCapability",
        "MedusaPIPCapability",
        "MedusaPinnedAppCapability",
        "MesaSerialNumber",
        "MetalCapability",
        "MicrophoneCapability",
        "MicrophoneCount",
        "MinimumSupportediTunesVersion",
        "MixAndMatchPrevention",
        "MobileDeviceMinimumVersion",
        "MobileEquipmentIdentifier",
        "MobileEquipmentInfoBaseId",
        "MobileEquipmentInfoBaseProfile",
        "MobileEquipmentInfoBaseVersion",
        "MobileEquipmentInfoCSN",
        "MobileEquipmentInfoDisplayCSN",
        "MobileSubscriberCountryCode",
        "MobileSubscriberNetworkCode",
        "MobileWifi",
        "ModelNumber",
        "MonarchLowEndHardware",
        "MultiLynxPublicKeyArray",
        "MultiLynxSerialNumberArray",
        "MultitaskingCapability",
        "MultitaskingGesturesCapability",
        "MusicStore",
        "MusicStoreCapability",
        "N78aHack",
        "NFCRadio",
        "NFCRadioCalibrationDataPresent",
        "NFCUniqueChipID",
        "NVRAMDictionary",
        "NandControllerUID",
        "NavajoFusingState",
        "NikeIpodCapability",
        "NotGreenTeaDeviceCapability",
        "OLEDDisplay",
        "OTAActivationCapability",
        "OfflineDictationCapability",
        "OpenGLES1Capability",
        "OpenGLES2Capability",
        "OpenGLES3Capability",
        "OpenGLESVersion",
        "PTPLargeFilesCapability",
        "PanelSerialNumber",
        "PanoramaCameraCapability",
        "PartitionType",
        "PasswordConfigured",
        "PasswordProtected",
        "PearlCameraCapability",
        "PearlIDCapability",
        "PeekUICapability",
        "PeekUIWidth",
        "Peer2PeerCapability",
        "PersonalHotspotCapability",
        "PhoneNumber",
        "PhoneNumber2",
        "PhosphorusCapability",
        "PhotoAdjustmentsCapability",
        "PhotoCapability",
        "PhotoSharingCapability",
        "PhotoStreamCapability",
        "PhotosPostEffectsCapability",
        "PiezoClickerCapability",
        "PintoMacAddress",
        "PintoMacAddressData",
        "PipelinedStillImageProcessingCapability",
        "PlatformStandAloneContactsCapability",
        "PlatinumCapability",
        "ProductHash",
        "ProductName",
        "ProductType",
        "ProductVersion",
        "ProximitySensorCalibration",
        "ProximitySensorCalibrationDictionary",
        "ProximitySensorCapability",
        "RF-exposure-separation-distance",
        "RFExposureSeparationDistance",
        "RawPanelSerialNumber",
        "RearCameraCapability",
        "RearCameraOffsetFromDisplayCenter",
        "RearFacingCamera60fpsVideoCaptureCapability",
        "RearFacingCameraAutoHDRCapability",
        "RearFacingCameraBurstCapability",
        "RearFacingCameraCapability",
        "RearFacingCameraHDRCapability",
        "RearFacingCameraHDROnCapability",
        "RearFacingCameraHFRCapability",
        "RearFacingCameraHFRVideoCapture1080pMaxFPS",
        "RearFacingCameraHFRVideoCapture720pMaxFPS",
        "RearFacingCameraMaxVideoZoomFactor",
        "RearFacingCameraModuleSerialNumber",
        "RearFacingCameraStillDurationForBurst",
        "RearFacingCameraSuperWideCameraCapability",
        "RearFacingCameraTimeOfFlightCameraCapability",
        "RearFacingCameraVideoCapture1080pMaxFPS",
        "RearFacingCameraVideoCapture4kMaxFPS",
        "RearFacingCameraVideoCapture720pMaxFPS",
        "RearFacingCameraVideoCaptureFPS",
        "RearFacingLowLightCameraCapability",
        "RearFacingSuperWideCameraModuleSerialNumber",
        "RearFacingTelephotoCameraCapability",
        "RearFacingTelephotoCameraModuleSerialNumber",
        "RecoveryOSVersion",
        "RegionCode",
        "RegionInfo",
        "RegionSupportsCinnamon",
        "RegionalBehaviorAll",
        "RegionalBehaviorChinaBrick",
        "RegionalBehaviorEUVolumeLimit",
        "RegionalBehaviorGB18030",
        "RegionalBehaviorGoogleMail",
        "RegionalBehaviorNTSC",
        "RegionalBehaviorNoPasscodeLocationTiles",
        "RegionalBehaviorNoVOIP",
        "RegionalBehaviorNoWiFi",
        "RegionalBehaviorShutterClick",
        "RegionalBehaviorValid",
        "RegionalBehaviorVolumeLimit",
        "RegulatoryModelNumber",
        "ReleaseType",
        "RemoteBluetoothAddress",
        "RemoteBluetoothAddressData",
        "RenderWideGamutImagesAtDisplayTime",
        "RendersLetterPressSlowly",
        "RequiredBatteryLevelForSoftwareUpdate",
        "RestoreOSBuild",
        "RestrictedCountryCodes",
        "RingerSwitchCapability",
        "RosalineSerialNumber",
        "RoswellChipID",
        "RotateToWakeStatus",
        "SBAllowSensitiveUI",
        "SBCanForceDebuggingInfo",
        "SDIOManufacturerTuple",
        "SDIOProductInfo",
        "SEInfo",
        "SEPNonce",
        "SIMCapability",
        "SIMPhonebookCapability",
        "SIMStatus",
        "SIMStatus2",
        "SIMTrayStatus",
        "SIMTrayStatus2",
        "SMSCapability",
        "SavageChipID",
        "SavageInfo",
        "SavageSerialNumber",
        "SavageUID",
        "ScreenDimensions",
        "ScreenDimensionsCapability",
        "ScreenRecorderCapability",
        "ScreenSerialNumber",
        "SecondaryBluetoothMacAddress",
        "SecondaryBluetoothMacAddressData",
        "SecondaryEthernetMacAddress",
        "SecondaryEthernetMacAddressData",
        "SecondaryWifiMacAddress",
        "SecondaryWifiMacAddressData",
        "SecureElement",
        "SecureElementID",
        "SecurityDomain",
        "SensitiveUICapability",
        "SerialNumber",
        "ShoeboxCapability",
        "ShouldHactivate",
        "SiKACapability",
        "SigningFuse",
        "SiliconBringupBoard",
        "SimultaneousCallAndDataCurrentlySupported",
        "SimultaneousCallAndDataSupported",
        "SiriGestureCapability",
        "SiriOfflineCapability",
        "Skey",
        "SoftwareBehavior",
        "SoftwareBundleVersion",
        "SoftwareDimmingAlpha",
        "SpeakerCalibrationMiGa",
        "SpeakerCalibrationSpGa",
        "SpeakerCalibrationSpTS",
        "SphereCapability",
        "StarkCapability",
        "StockholmJcopInfo",
        "StrictWakeKeyboardCases",
        "SupportedDeviceFamilies",
        "SupportedKeyboards",
        "SupportsBurninMitigation",
        "SupportsEDUMU",
        "SupportsForceTouch",
        "SupportsIrisCapture",
        "SupportsLowPowerMode",
        "SupportsPerseus",
        "SupportsRotateToWake",
        "SupportsSOS",
        "SupportsSSHBButtonType",
        "SupportsTouchRemote",
        "SysCfg",
        "SysCfgDict",
        "SystemImageID",
        "SystemTelephonyOfAnyKindCapability",
        "TVOutCrossfadeCapability",
        "TVOutSettingsCapability",
        "TelephonyCapability",
        "TelephonyMaximumGeneration",
        "TimeSyncCapability",
        "TopModuleAuthChipID",
        "TouchDelivery120Hz",
        "TouchIDCapability",
        "TristarID",
        "UIBackgroundQuality",
        "UIParallaxCapability",
        "UIProceduralWallpaperCapability",
        "UIReachability",
        "UMTSDeviceCapability",
        "UnifiedIPodCapability",
        "UniqueChipID",
        "UniqueDeviceID",
        "UniqueDeviceIDData",
        "UserAssignedDeviceName",
        "UserIntentPhysicalButtonCGRect",
        "UserIntentPhysicalButtonCGRectString",
        "UserIntentPhysicalButtonNormalizedCGRect",
        "VOIPCapability",
        "VeniceCapability",
        "VibratorCapability",
        "VideoCameraCapability",
        "VideoStillsCapability",
        "VoiceControlCapability",
        "VolumeButtonCapability",
        "WAGraphicQuality",
        "WAPICapability",
        "WLANBkgScanCache",
        "WSKU",
        "WatchCompanionCapability",
        "WatchSupportsAutoPlaylistPlayback",
        "WatchSupportsHighQualityClockFaceGraphics",
        "WatchSupportsListeningOnGesture",
        "WatchSupportsMusicStreaming",
        "WatchSupportsSiriCommute",
        "WiFiCallingCapability",
        "WiFiCapability",
        "WifiAddress",
        "WifiAddressData",
        "WifiAntennaSKUVersion",
        "WifiCallingSecondaryDeviceCapability",
        "WifiChipset",
        "WifiFirmwareVersion",
        "WifiVendor",
        "WirelessBoardSnum",
        "WirelessChargingCapability",
        "YonkersChipID",
        "YonkersSerialNumber",
        "YonkersUID",
        "YouTubeCapability",
        "YouTubePluginCapability",
        "accelerometer",
        "accessibility",
        "additional-text-tones",
        "aggregate-cam-photo-zoom",
        "aggregate-cam-video-zoom",
        "airDropRestriction",
        "airplay-mirroring",
        "airplay-no-mirroring",
        "all-features",
        "allow-32bit-apps",
        "ambient-light-sensor",
        "ane",
        "any-telephony",
        "apn",
        "apple-internal-install",
        "applicationInstallation",
        "arkit",
        "arm64",
        "armv6",
        "armv7",
        "armv7s",
        "assistant",
        "auto-focus",
        "auto-focus-camera",
        "baseband-chipset",
        "bitrate-2g",
        "bitrate-3g",
        "bitrate-lte",
        "bitrate-wifi",
        "bluetooth",
        "bluetooth-le",
        "board-id",
        "boot-manifest-hash",
        "boot-nonce",
        "builtin-mics",
        "c2k-device",
        "calibration",
        "call-forwarding",
        "call-waiting",
        "caller-id",
        "camera-flash",
        "camera-front",
        "camera-front-flash",
        "camera-rear",
        "cameraRestriction",
        "car-integration",
        "cell-broadcast",
        "cellular-data",
        "certificate-production-status",
        "certificate-security-mode",
        "chip-id",
        "class",
        "closed-loop",
        "config-number",
        "contains-cellular-radio",
        "crypto-hash-method",
        "dali-mode",
        "data-plan",
        "debug-board-revision",
        "delay-sleep-for-headset-click",
        "device-color-policy",
        "device-colors",
        "device-name",
        "device-name-localized",
        "dictation",
        "die-id",
        "display-mirroring",
        "display-rotation",
        "displayport",
        "does-not-support-gamekit",
        "effective-production-status",
        "effective-production-status-ap",
        "effective-production-status-sep",
        "effective-security-mode",
        "effective-security-mode-ap",
        "effective-security-mode-sep",
        "enc-top-type",
        "encode-aac",
        "encrypted-data-partition",
        "enforce-googlemail",
        "enforce-shutter-click",
        "euicc-chip-id",
        "explicitContentRestriction",
        "face-detection-support",
        "fast-switch-options",
        "fcc-logos-via-software",
        "fcm-type",
        "firmware-version",
        "flash",
        "front-auto-hdr",
        "front-burst",
        "front-burst-image-duration",
        "front-facing-camera",
        "front-flash-capability",
        "front-hdr",
        "front-hdr-on",
        "front-max-video-fps-1080p",
        "front-max-video-fps-4k",
        "front-max-video-fps-720p",
        "front-max-video-zoom",
        "front-slowmo",
        "full-6",
        "function-button_halleffect",
        "function-button_ringerab",
        "gamekit",
        "gas-gauge-battery",
        "gps",
        "gps-capable",
        "green-tea",
        "gyroscope",
        "h264-encoder",
        "hall-effect-sensor",
        "haptics",
        "hardware-keyboard",
        "has-sphere",
        "hd-video-capture",
        "hdr-image-capture",
        "healthkit",
        "hearingaid-audio-equalization",
        "hearingaid-low-energy-audio",
        "hearingaid-power-reduction",
        "hiccough-interval",
        "hide-non-default-apps",
        "hidpi",
        "home-button-type",
        "homescreen-wallpaper",
        "hw-encode-snapshots",
        "hw-snapshots-need-purplegfx",
        "iAP2Capability",
        "iPadCapability",
        "iTunesFamilyID",
        "iap2-protocol-supported",
        "image4-supported",
        "international-settings",
        "io-surface-backed-images",
        "ipad",
        "kConferenceCallType",
        "kSimultaneousCallAndDataCurrentlySupported",
        "kSimultaneousCallAndDataSupported",
        "large-format-phone",
        "live-effects",
        "live-photo-capture",
        "load-thumbnails-while-scrolling",
        "location-reminders",
        "location-services",
        "low-power-wallet-mode",
        "lte-device",
        "magnetometer",
        "main-screen-class",
        "main-screen-height",
        "main-screen-orientation",
        "main-screen-pitch",
        "main-screen-scale",
        "main-screen-width",
        "marketing-name",
        "mesa",
        "metal",
        "microphone",
        "mix-n-match-prevention-status",
        "mms",
        "modelIdentifier",
        "multi-touch",
        "multitasking",
        "multitasking-gestures",
        "n78a-mode",
        "name",
        "navigation",
        "nfc",
        "nfcWithRadio",
        "nike-ipod",
        "nike-support",
        "no-coreroutine",
        "no-hi-res-buildings",
        "no-simplistic-road-mesh",
        "not-green-tea",
        "offline-dictation",
        "opal",
        "opengles-1",
        "opengles-2",
        "opengles-3",
        "opposed-power-vol-buttons",
        "ota-activation",
        "panorama",
        "peek-ui-width",
        "peer-peer",
        "personal-hotspot",
        "photo-adjustments",
        "photo-stream",
        "piezo-clicker",
        "pipelined-stillimage-capability",
        "platinum",
        "post-effects",
        "pressure",
        "prox-sensor",
        "proximity-sensor",
        "ptp-large-files",
        "public-key-accelerator",
        "rear-auto-hdr",
        "rear-burst",
        "rear-burst-image-duration",
        "rear-cam-telephoto-capability",
        "rear-facing-camera",
        "rear-hdr",
        "rear-hdr-on",
        "rear-max-slomo-video-fps-1080p",
        "rear-max-slomo-video-fps-720p",
        "rear-max-video-fps-1080p",
        "rear-max-video-fps-4k",
        "rear-max-video-fps-720p",
        "rear-max-video-frame_rate",
        "rear-max-video-zoom",
        "rear-slowmo",
        "regulatory-model-number",
        "ringer-switch",
        "role",
        "s8000\")",
        "s8003\")",
        "sandman-support",
        "screen-dimensions",
        "sensitive-ui",
        "shoebox",
        "sika-support",
        "sim",
        "sim-phonebook",
        "siri-gesture",
        "slow-letterpress-rendering",
        "sms",
        "software-bundle-version",
        "software-dimming-alpha",
        "stand-alone-contacts",
        "still-camera",
        "stockholm",
        "supports-always-listening",
        "t7000\")",
        "telephony",
        "telephony-maximum-generation",
        "thin-bezel",
        "tnr-mode-back",
        "tnr-mode-front",
        "touch-id",
        "tv-out-crossfade",
        "tv-out-settings",
        "ui-background-quality",
        "ui-no-parallax",
        "ui-no-procedural-wallpaper",
        "ui-pip",
        "ui-reachability",
        "ui-traffic-cheap-shaders",
        "ui-weather-quality",
        "umts-device",
        "unified-ipod",
        "unique-chip-id",
        "venice",
        "video-camera",
        "video-cap",
        "video-stills",
        "voice-control",
        "voip",
        "volume-buttons",
        "wapi",
        "watch-companion",
        "wi-fi",
        "wifi",
        "wifi-antenna-sku-info",
        "wifi-chipset",
        "wifi-module-sn",
        "wlan",
        "wlan.background-scan-cache",
        "youtube",
        "youtubePlugin"};

    // Create checkboxes for each key
    for (const QString &key : mobileGestaltKeys) {
        QCheckBox *checkbox = new QCheckBox(key);
        checkbox->setStyleSheet("QCheckBox { margin: 2px; }");
        keyCheckboxes.append(checkbox);
        checkboxLayout->addWidget(checkbox);
    }
}

QStringList QueryMobileGestaltWidget::getSelectedKeys()
{
    QStringList selectedKeys;
    for (QCheckBox *checkbox : keyCheckboxes) {
        if (checkbox->isChecked()) {
            selectedKeys.append(checkbox->text());
        }
    }
    return selectedKeys;
}

void QueryMobileGestaltWidget::onQueryButtonClicked()
{
    QStringList selectedKeys = getSelectedKeys();

    if (selectedKeys.isEmpty()) {
        statusLabel->setText("Please select at least one key to query.");
        statusLabel->setStyleSheet("color: #ff6b6b; font-style: italic;");
        return;
    }

    statusLabel->setText(
        QString("Querying %1 key(s)...").arg(selectedKeys.size()));
    statusLabel->setStyleSheet("color: #4CAF50; font-style: italic;");

    QMap<QString, QVariant> results = queryMobileGestalt(selectedKeys);

    displayResults(results);

    statusLabel->setText(
        QString("Query completed. Found %1 result(s).").arg(results.size()));
}

void QueryMobileGestaltWidget::onSelectAllClicked()
{
    for (QCheckBox *checkbox : keyCheckboxes) {
        checkbox->setChecked(true);
    }
}

void QueryMobileGestaltWidget::onClearAllClicked()
{
    for (QCheckBox *checkbox : keyCheckboxes) {
        checkbox->setChecked(false);
    }
}

void QueryMobileGestaltWidget::displayResults(
    const QMap<QString, QVariant> &results)
{
    QString output;
    output += "MobileGestalt Query Results\n";
    output += "=" + QString("=").repeated(50) + "\n\n";

    if (results.isEmpty()) {
        output += "No results found.\n";
    } else {
        for (auto it = results.begin(); it != results.end(); ++it) {
            output += QString("Key: %1\n").arg(it.key());
            output += QString("Value: %1\n").arg(it.value().toString());
            output += QString("-").repeated(30) + "\n";
        }
    }

    outputTextEdit->setPlainText(output);
}

QMap<QString, QVariant>
QueryMobileGestaltWidget::queryMobileGestalt(const QStringList &keys)
{
    char *xml = nullptr;
    uint32_t xmlLength = 0;

    std::vector<char *> keys_vector;
    for (const QString &key : keys) {
        keys_vector.push_back(key.toUtf8().data());
    }

    auto res = m_device->diagRelay->mobilegestalt(keys_vector);

    if (!res.is_ok()) {
        qDebug() << "MobileGestalt query failed.";
        return {};
    }

    plist_t res_plist_raw = res.unwrap().unwrap();
    // TODO: safety ?
    auto res_plist = PlistNavigator(res_plist_raw)["MobileGestalt"];
    plist_print(res_plist_raw);
    QMap<QString, QVariant> results;
    for (const QString &key : keys) {
        // Use the new toQVariant method to get the value
        QVariant value = res_plist[key.toStdString()].toQVariant();
        qDebug() << "Key:" << key
                 << "Value:" << value; // Print QVariant directly
        if (!value.isNull()) { // Only insert if the QVariant is valid (key
                               // exists and value could be parsed)
            results.insert(key, value);
        }
    }
    plist_free(res_plist_raw);
    return results;
}