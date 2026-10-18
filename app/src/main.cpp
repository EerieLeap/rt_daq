#include <memory>

#include <eerie_memory.hpp>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "utilities/dev_tools/system_info.h"
#include "utilities/memory/memory_resource_manager.h"

#include "subsys/device_tree/dt_configurator.h"
#include "subsys/device_tree/dt_display.h"
#include "subsys/device_tree/dt_fs.h"
#include "subsys/device_tree/dt_gpio.h"

#include "subsys/assets/assets_manager.h"
#include "subsys/cfb/cfb.h"
#include "subsys/fs/services/fs_service.h"
#include "subsys/fs/services/sdmmc_service.h"
#include "subsys/gpio/gpio_factory.hpp"
#include "subsys/threading/work_queue_thread.h"
#include "subsys/time/boot_elapsed_time_provider.h"
#include "subsys/time/rtc_provider.h"
#include "subsys/time/time_service.h"

#include "configuration/services/cbor_configuration_service.h"

#include "domain/canbus_domain/configuration/canbus_configuration_manager.h"
#include "domain/canbus_domain/services/canbus_scheduler_service.h"
#include "domain/configuration_domain/services/configuration_service.h"
#include "domain/logging_domain/configuration/logging_configuration_manager.h"
#include "domain/logging_domain/control/canbus_com_logging_command_sender.h"
#include "domain/logging_domain/control/configuration_update_logging_stop.h"
#include "domain/sensor_domain/configuration/sensors_configuration_manager.h"
#include "domain/sensor_domain/services/sensor_events_publisher.h"
#include "domain/sensor_domain/utilities/sensor_readings_frame.hpp"
#include "domain/system_domain/configuration/system_configuration_manager.h"

#include "controllers/adc_controller.h"
#include "controllers/canbus_controller.h"
#include "controllers/logging_controller.h"
#include "controllers/sensors_controller.h"
#include "controllers/system_controller.h"
#ifdef CONFIG_APP_LOGGING_SHELL
    #include "shell/logging_shell_bridge.h"
#endif
#ifdef CONFIG_APP_SENSOR_SHELL
    #include "shell/sensor_shell_bridge.h"
#endif
#include "controllers/display_controller.h"
#include "controllers/management_controller.h"

#include "event_bus/event_channels.h"

// Test sensors includes
#include "domain/sensor_domain/models/sensor.h"

#include "utilities/voltage_interpolator/calibration_data.h"
#include "utilities/voltage_interpolator/interpolation_method.h"

using namespace eerie_leap::utilities::voltage_interpolator;
using namespace eerie_leap::domain::sensor_domain::configuration;
using namespace eerie_leap::domain::sensor_domain::models;
// End test sensors

using namespace eerie_memory;
using namespace eerie_leap::utilities::memory;
using namespace eerie_leap::utilities::dev_tools;

using namespace eerie_leap::subsys::device_tree;
using eerie_leap::subsys::assets::AssetsManager;
using namespace eerie_leap::subsys::fs::services;
using namespace eerie_leap::subsys::gpio;
using namespace eerie_leap::subsys::cfb;
using namespace eerie_leap::subsys::threading;
using namespace eerie_leap::subsys::time;
using namespace eerie_leap::subsys::canbus;

using namespace eerie_leap::configuration::services;

using namespace eerie_leap::domain::configuration_domain::services;
using namespace eerie_leap::domain::canbus_domain::models;
using namespace eerie_leap::domain::canbus_domain::services;
using namespace eerie_leap::domain::canbus_domain::configuration;
using namespace eerie_leap::domain::system_domain::configuration;
using namespace eerie_leap::domain::sensor_domain::models;
using namespace eerie_leap::domain::sensor_domain::configuration;
using namespace eerie_leap::domain::sensor_domain::services;
using namespace eerie_leap::domain::sensor_domain::utilities;
using namespace eerie_leap::domain::logging_domain::models;
using namespace eerie_leap::domain::logging_domain::services;
using namespace eerie_leap::domain::logging_domain::configuration;
using namespace eerie_leap::domain::logging_domain::control;

using namespace eerie_leap::controllers;
using namespace eerie_leap::event_bus;

LOG_MODULE_REGISTER(main_logger);

constexpr uint32_t SLEEP_TIME_MS = 10000;

static void SetupCanbusConfiguration(const std::shared_ptr<CanbusConfigurationManager>& canbus_configuration_manager);
static void SetupTestSensors(const std::shared_ptr<SensorsConfigurationManager>& sensors_configuration_manager);
static void SetupLoggingConfiguration(
    const std::shared_ptr<SensorsConfigurationManager>& sensors_configuration_manager,
    const std::shared_ptr<LoggingConfigurationManager>& logging_configuration_manager
);

// NOTE: ADC reading timing for ESP32S3:
// Each sample adds up to about 25us to the reading time

// NOTE: Sensor reading timing for ESP32S3:
// ADC reading with 40 samples and no expression evaluation takes around 1200us
// ADC reading with 40 samples and expression evaluation takes minimum of 3300us
// GPIO reading with no expression evaluation takes around 130us
// GPIO reading with expression evaluation takes minimum of 1900us
// Formula evaluation takes minimum of 1800us, applicable to virtual sensors

int main(void) {
    // Channels are inert until a bus registers them, so this comes before anything publishes or
    // subscribes.
    InitializeEventChannels();

    DtConfigurator::Initialize(
        DtFeature::INTERNAL_FS | DtFeature::SD_FS | DtFeature::GPIO | DtFeature::ADC | DtFeature::DISPLAY
        | DtFeature::CANBUS
    );

    std::shared_ptr<Cfb> cfb = nullptr;

    if(DtDisplay::Get() != nullptr) {
        cfb = std::make_shared<Cfb>(DtDisplay::Get());

        if(!cfb->Initialize()) {
            LOG_ERR("Failed to initialize CFB.");
            return -1;
        }
    }

    const auto display_controller = std::make_shared<DisplayController>(cfb);
    // NOTE: The display runs at priority 6, below sensor processing (5 in prj.conf).
    // At equal priority it delayed processing wakes by up to ~5ms (ADC plan, sections 2.7, 6.7).
    display_controller->Initialize();

    std::shared_ptr<SdmmcService> sd_fs_service = nullptr;
    auto* sd_fs_mp = DtFs::GetSdFsMp();
    if(sd_fs_mp != nullptr) {
        sd_fs_service = std::make_shared<SdmmcService>(sd_fs_mp, DtFs::GetSdDiskName());
        sd_fs_service->RegisterIsSdCardPresentHandler(DtFs::IsSdCardPresent);
        if(!sd_fs_service->Initialize()) {
            LOG_ERR("Failed to initialize SD File System.");
        }

        // TODO: SPI SD card detection causes card to be
        // constatly mounted/umounted during logging
        // sd_fs_service->SdMonitorStart();
    }

    const auto fs_service = std::make_shared<FsService>(DtFs::GetInternalFsMp());
    if(!fs_service->Initialize()) {
        LOG_ERR("Failed to initialize File System.");
        return -1;
    }

    // Shared: controllers register the asset kinds they own, and the management controller serves them.
    const auto assets_manager = std::make_shared<AssetsManager>();

    const auto rtc_provider = std::make_shared<RtcProvider>();
    const auto boot_elapsed_time_provider = std::make_shared<BootElapsedTimeProvider>();
    const auto time_service = std::make_shared<TimeService>(rtc_provider, boot_elapsed_time_provider);
    time_service->Initialize();

    // Every start and stop of the log writer runs here, whatever requested it (the CDMP command, a
    // trigger, the shell, the autostart), besides the configuration applies, which restart sensor
    // processing and, before LoggingController::Start() or for a start requested after an update,
    // the writer. A start builds the logger and writes the MDF header through FatFs:
    // 5728 of 6144 bytes measured on the PM board (logging plan section 6.5), too close to keep.
    const int config_work_queue_stack_size = 8192;
    const int config_work_queue_priority = 5;
    const auto config_work_queue_thread = std::make_shared<WorkQueueThread>(
        "config_work_queue", config_work_queue_stack_size, config_work_queue_priority
    );
    if(!config_work_queue_thread->Initialize()) {
        LOG_ERR("Failed to initialize the configuration work queue.");
        return -1;
    }

    // Before anything can publish a logging status.
    display_controller->FollowLoggingStatus(config_work_queue_thread);

    const auto system_controller = std::make_shared<SystemController>(fs_service, config_work_queue_thread);
    if(system_controller->Initialize() != 0)
        LOG_ERR("Failed to initialize the system controller.");

    const auto configuration_service = std::make_shared<ConfigurationService>();
    configuration_service->RegisterCborConfigurationManager(
        ConfigurationService::Type::System, system_controller->GetConfigurationManager()
    );

    const auto adc_controller = std::make_shared<AdcController>(
        fs_service, config_work_queue_thread, configuration_service, time_service
    );
    if(adc_controller->Initialize() != 0) {
        LOG_ERR("Failed to initialize the ADC controller.");
        return -1;
    }

    std::shared_ptr<IGpio> gpio = GpioFactory(DtGpio::Get).Create();
    if(gpio->Initialize() != 0) {
        LOG_ERR("Failed to initialize GPIO.");
        gpio = nullptr;
    }

    const auto canbus_controller = std::make_shared<CanbusController>(
        fs_service, config_work_queue_thread, configuration_service, sd_fs_service, assets_manager
    );
    // TODO: For test purposes only
    // if(canbus_controller->Initialize(SetupCanbusConfiguration) != 0)
    if(canbus_controller->Initialize() != 0)
        LOG_ERR("Failed to initialize the CANBus controller.");
    canbus_controller->Start();

    const auto sensor_readings_frame = std::make_shared<SensorReadingsFrame>();

    const auto sensors_controller = std::make_shared<SensorsController>(
        fs_service, config_work_queue_thread, configuration_service, time_service, sensor_readings_frame,
        canbus_controller->GetService(), gpio, adc_controller->GetManager(), adc_controller->GetConfigurationManager(),
        sd_fs_service, assets_manager
    );
    // TODO: For test purposes only
    // if(sensors_controller->Initialize(SetupTestSensors) != 0)
    if(sensors_controller->Initialize() != 0)
        LOG_ERR("Failed to initialize the sensors controller.");

    adc_controller->RegisterPausedService(sensors_controller->GetProcessingService());
    canbus_controller->RegisterDependentService(sensors_controller->GetProcessingService());

#ifdef CONFIG_APP_SENSOR_SHELL
    // Independent of the logging controller: a board without an SD card can still drive CAN messages
    // and other units' triggers.
    eerie_leap::shell::AttachSensorShell(
        sensor_readings_frame, sensors_controller->GetConfigurationManager(),
        sensors_controller->GetProcessingService(), time_service, config_work_queue_thread
    );
#endif

    const auto canbus_com_service = canbus_controller->GetComService();

    // One per unit, on the shared frame. It publishes only what a subscriber accepts: on this unit the
    // sensors the logging triggers bind to. The frame stays empty until SensorsController::Start().
    const auto sensor_events_publisher = std::make_shared<SensorEventsPublisher>(sensor_readings_frame);
    if(!sensor_events_publisher->Initialize() || !sensor_events_publisher->Start())
        LOG_ERR("Failed to start the sensor events publisher.");

    // Only an initialized controller is kept, so a non-null one is what the dependent, the shell, the
    // test configuration and the autostart below can rely on.
    std::shared_ptr<LoggingController> logging_controller = nullptr;
    // Only without a logging controller: the stop of a configuration update, with no log writer to stop.
    std::unique_ptr<ConfigurationUpdateLoggingStop> configuration_update_logging_stop;
    if(sd_fs_service != nullptr) {
        auto controller = std::make_shared<LoggingController>(
            fs_service, config_work_queue_thread, configuration_service, sensors_controller->GetConfigurationManager(),
            canbus_com_service, sensor_events_publisher, sd_fs_service, time_service, sensor_readings_frame, "rt_daq",
            assets_manager
        );
        // A failed controller has torn itself down and registered nothing; dropping it here leaves
        // nothing behind, and nothing below is wired to it.
        if(controller->Initialize() == 0)
            logging_controller = std::move(controller);
        else
            LOG_ERR("Failed to initialize the logging controller.");
    } else {
        LOG_INF("No SD file system: no logging, logging triggers or LOGGING command on this unit.");
    }

    if(logging_controller != nullptr) {
        // Stopped before a sensors restart drops the generation the writer reads, and started again
        // on the new one while logging is still wanted: a sensors store during boot, or one followed
        // by a new start request, finalizes the file and opens the next. After
        // logging_controller->Start() a stored configuration stops logging first (L9), and the
        // dependent finds the writer stopped.
        sensors_controller->RegisterDependentService(logging_controller->GetDependentService());

#ifdef CONFIG_APP_LOGGING_SHELL
        eerie_leap::shell::AttachLoggingShell(
            logging_controller, sensors_controller->GetConfigurationManager(), config_work_queue_thread
        );
#endif

        // TODO: For test purposes only
#ifdef CONFIG_APP_SETUP_LOGGING_CONFIGURATION
        SetupLoggingConfiguration(
            sensors_controller->GetConfigurationManager(), logging_controller->GetConfigurationManager()
        );
#endif

        // Last of the boot's stores: from here on a configuration stored on this unit stops logging on
        // every unit.
        if(logging_controller->Start() != 0)
            LOG_ERR("Failed to start the logging controller.");
    } else if(canbus_com_service != nullptr) {
        // No logging here, but what this unit measures and sends goes into the files of the units that
        // log, so a configuration stored on it stops their logging too.
        configuration_update_logging_stop = std::make_unique<ConfigurationUpdateLoggingStop>(
            configuration_service, config_work_queue_thread, nullptr,
            std::make_shared<CanbusComLoggingCommandSender>(canbus_com_service)
        );
        if(!configuration_update_logging_stop->Initialize() || !configuration_update_logging_stop->Start())
            LOG_ERR("Failed to start the configuration update stop.");
    }

    // Constructed after every controller that registers a configuration manager.
    const auto system_configuration = system_controller->GetConfigurationManager()->Get();
    const auto management_controller = std::make_shared<ManagementController>(
        configuration_service, config_work_queue_thread,
        canbus_com_service != nullptr ? canbus_com_service->GetNetworkInfo() : nullptr,
        system_configuration != nullptr ? system_configuration->build_number : 0, sensors_controller->GetCapabilities(),
        sd_fs_service, assets_manager
    );
    if(management_controller->Initialize() != 0)
        LOG_ERR("Failed to initialize the management controller.");

    const auto canbus_scheduler_service = std::make_shared<CanbusSchedulerService>(
        canbus_controller->GetConfigurationManager(), canbus_controller->GetService(), sensor_readings_frame
    );
    canbus_scheduler_service->Initialize();
    canbus_controller->RegisterDependentService(canbus_scheduler_service);

    sensors_controller->Start();
    canbus_scheduler_service->Start();

#if CONFIG_APP_LOGGING_AUTOSTART_DELAY_MS > 0
    // Bench aid: stands in for the CDMP LOGGING command or a trigger when neither is at hand. The
    // supervisor logs a start that fails.
    if(logging_controller != nullptr) {
        k_msleep(CONFIG_APP_LOGGING_AUTOSTART_DELAY_MS);
        // This unit only: the bench aid stands in for a local trigger.
        logging_controller->GetControl()->RequestStart(logging_controller->GetUnitUid());
        LOG_INF("Logging start requested automatically.");
    }
#endif

    while(true) {
        // SystemInfo::PrintHeapInfo();
        // SystemInfo::PrintStackInfo();
        k_msleep(SLEEP_TIME_MS);
    }

    return 0;
}

void SetupCanbusConfiguration(const std::shared_ptr<CanbusConfigurationManager>& canbus_configuration_manager) {
    const auto canbus_configuration = make_shared_pmr<CanbusConfiguration>(Mrm::GetExtPmr());
    canbus_configuration->com_configuration.bus_channel = 0;

    // ============================================
    // CANBus 0
    // ============================================

    CanChannelConfiguration canbus_channel_configuration_0(std::allocator_arg, Mrm::GetExtPmr());
    canbus_channel_configuration_0.type = CanbusType::CLASSICAL_CAN;
    canbus_channel_configuration_0.is_extended_id = false;
    canbus_channel_configuration_0.bus_channel = 0;
    canbus_channel_configuration_0.bitrate = 1'000'000;
    // canbus_channel_configuration_0.data_bitrate = 2000000;

    // auto message_configuration_0 = make_shared_pmr<CanMessageConfiguration>(Mrm::GetExtPmr());
    // message_configuration_0->frame_id = 790;
    // message_configuration_0->send_interval_ms = 10;
    // canbus_channel_configuration_0.message_configurations.emplace_back(std::move(message_configuration_0));

    // auto message_configuration_1 = make_shared_pmr<CanMessageConfiguration>(Mrm::GetExtPmr());
    // message_configuration_1->frame_id = 809;
    // message_configuration_1->send_interval_ms = 10;
    // canbus_channel_configuration_0.message_configurations.emplace_back(std::move(message_configuration_1));

    // auto message_configuration_2 = make_shared_pmr<CanMessageConfiguration>(Mrm::GetExtPmr());
    // message_configuration_2->frame_id = 1087;
    // message_configuration_2->send_interval_ms = 10;
    // message_configuration_2->script_path = "scripts/e46_smg_gear.lua";
    // canbus_channel_configuration_0.message_configurations.emplace_back(std::move(message_configuration_2));

    // auto message_configuration_3 = make_shared_pmr<CanMessageConfiguration>(Mrm::GetExtPmr());
    // message_configuration_3->frame_id = 100;
    // message_configuration_3->send_interval_ms = 10;
    // message_configuration_3->name = "EL_FRAME_0";
    // message_configuration_3->message_size = 8;

    // CanSignalConfiguration signal_configuration_0(std::allocator_arg, Mrm::GetExtPmr());
    // signal_configuration_0.start_bit = 16;
    // signal_configuration_0.size_bits = 16;
    // signal_configuration_0.name = "sensor_1";
    // signal_configuration_0.unit = "km/h";
    // message_configuration_3->signal_configurations.emplace_back(std::move(signal_configuration_0));
    // canbus_channel_configuration_0.message_configurations.emplace_back(std::move(message_configuration_3));

    // for(int i = 0; i < 20; i++) {
    //     auto message_configuration = make_shared_pmr<CanMessageConfiguration>(Mrm::GetExtPmr());
    //     message_configuration->frame_id = 100 + i;
    //     message_configuration->send_interval_ms = 10;
    //     message_configuration->name = "EL_FRAME_" + std::to_string(i);
    //     message_configuration->message_size = 8 * 8;

    //     for(int j = 0; j < 4 * 8; j++) {
    //         CanSignalConfiguration signal_configuration(std::allocator_arg, Mrm::GetExtPmr());
    //         signal_configuration.start_bit = j * 16;
    //         signal_configuration.size_bits = 16;
    //         signal_configuration.name = "sensor_" + std::to_string(j);
    //         signal_configuration.unit = "km/h";
    //         message_configuration->signal_configurations.emplace_back(std::move(signal_configuration));
    //     }

    //     canbus_channel_configuration_0.message_configurations.emplace_back(std::move(message_configuration));
    // }

    // NOTE: This frame is transmitted at 20ms interval
    //       with sensor_1 value
    // auto message_configuration_0 = make_shared_pmr<CanMessageConfiguration>(Mrm::GetExtPmr());
    // message_configuration_0->name = "EL_FRAME_0";
    // message_configuration_0->message_size = 8;
    // message_configuration_0->frame_id = 790;
    // message_configuration_0->send_interval_ms = 20;

    // CanSignalConfiguration signal_configuration_0(std::allocator_arg, Mrm::GetExtPmr());
    // signal_configuration_0.start_bit = 16;
    // signal_configuration_0.size_bits = 16;
    // signal_configuration_0.name = "sensor_1";
    // signal_configuration_0.unit = "km/h";
    // signal_configuration_0.factor = 0.1;
    // message_configuration_0->signal_configurations.emplace_back(std::move(signal_configuration_0));
    // canbus_channel_configuration_0.message_configurations.emplace_back(std::move(message_configuration_0));

    for(int i = 0; i < 3; i++) {
        const auto message_configuration = make_shared_pmr<CanMessageConfiguration>(Mrm::GetExtPmr());
        message_configuration->frame_id = 100 + i;
        message_configuration->name = "EL_FRAME_" + std::to_string(i);
        message_configuration->message_size = 8;
        message_configuration->send_interval_ms = 20;

        CanSignalConfiguration signal_configuration(std::allocator_arg, Mrm::GetExtPmr());
        signal_configuration.start_bit = 0;
        signal_configuration.size_bits = 16;
        signal_configuration.name = "sensor_" + std::to_string(i);
        signal_configuration.unit = "km/h";
        message_configuration->signal_configurations.emplace_back(std::move(signal_configuration));

        canbus_channel_configuration_0.message_configurations.emplace_back(std::move(message_configuration));
    }

    canbus_configuration->channel_configurations.emplace(
        canbus_channel_configuration_0.bus_channel, std::move(canbus_channel_configuration_0)
    );

    // ============================================
    // CANBus 1
    // ============================================

    // NOTE: This frame is expected to be received
    //       managed by sensor_6
    CanChannelConfiguration canbus_channel_configuration_1(std::allocator_arg, Mrm::GetExtPmr());
    canbus_channel_configuration_1.type = CanbusType::CLASSICAL_CAN;
    canbus_channel_configuration_1.is_extended_id = false;
    canbus_channel_configuration_1.bus_channel = 1;
    canbus_channel_configuration_1.bitrate = 1'000'000;

    const auto message_configuration_1_0 = make_shared_pmr<CanMessageConfiguration>(Mrm::GetExtPmr());
    message_configuration_1_0->name = "EL_FRAME_1_0";
    message_configuration_1_0->message_size = 8;
    message_configuration_1_0->frame_id = 790;

    CanSignalConfiguration signal_configuration_1_0(std::allocator_arg, Mrm::GetExtPmr());
    signal_configuration_1_0.start_bit = 16;
    signal_configuration_1_0.size_bits = 16;
    signal_configuration_1_0.name = "RPM";
    signal_configuration_1_0.unit = "RPM";
    signal_configuration_1_0.factor = 0.156;
    message_configuration_1_0->signal_configurations.emplace_back(std::move(signal_configuration_1_0));
    canbus_channel_configuration_1.message_configurations.emplace_back(std::move(message_configuration_1_0));

    canbus_configuration->channel_configurations.emplace(
        canbus_channel_configuration_1.bus_channel, std::move(canbus_channel_configuration_1)
    );

    canbus_configuration_manager->Update(*canbus_configuration);
}

void SetupTestSensors(const std::shared_ptr<SensorsConfigurationManager>& sensors_configuration_manager) {
    // Test Sensors

    const auto sensor_1 = make_shared_pmr<Sensor>(Mrm::GetExtPmr(), "sensor_1");

    sensor_1->metadata.name = "Sensor 1";
    sensor_1->metadata.unit = "km/h";
    sensor_1->metadata.description = "Test Sensor 1";

    sensor_1->configuration.type = SensorType::PHYSICAL_ANALOG;
    sensor_1->configuration.channel = 0;
    // sensor_1->configuration.script_path = "scripts/sensor_1.lua";
    sensor_1->configuration.sampling_rate_ms = 50;
    sensor_1->configuration.interpolation_method = InterpolationMethod::LINEAR;
    sensor_1->configuration.calibration_table = { { 0.0, 0.0 }, { 5.0, 100.0 } };
    // sensor_1->configuration.expression = "x * 2 + sensor_2 + 1";

    const auto sensor_2 = make_shared_pmr<Sensor>(Mrm::GetExtPmr(), "sensor_2");

    sensor_2->metadata.name = "Sensor 2";
    sensor_2->metadata.unit = "km/h";
    sensor_2->metadata.description = "Test Sensor 2";

    sensor_2->configuration.type = SensorType::PHYSICAL_ANALOG;
    sensor_2->configuration.channel = 1;
    sensor_2->configuration.sampling_rate_ms = 1000;
    sensor_2->configuration.interpolation_method = InterpolationMethod::CUBIC_SPLINE;
    sensor_2->configuration.calibration_table = { { 0.0, 90.0 }, { 5.0, 100.0 } };
    sensor_2->configuration.expression = "x * 4 + 1.6";

    const auto sensor_3 = make_shared_pmr<Sensor>(Mrm::GetExtPmr(), "sensor_3");

    sensor_3->metadata.name = "Sensor 3";
    sensor_3->metadata.unit = "km/h";
    sensor_3->metadata.description = "Test Sensor 3";

    sensor_3->configuration.type = SensorType::VIRTUAL_ANALOG;
    sensor_3->configuration.sampling_rate_ms = 2000;
    sensor_3->configuration.expression = "2 + 8.34";

    const auto sensor_4 = make_shared_pmr<Sensor>(Mrm::GetExtPmr(), "sensor_4");

    sensor_4->metadata.name = "Sensor 4";
    sensor_4->metadata.unit = "";
    sensor_4->metadata.description = "Test Sensor 4";

    sensor_4->configuration.type = SensorType::PHYSICAL_INDICATOR;
    sensor_4->configuration.channel = 1;
    sensor_4->configuration.sampling_rate_ms = 1000;

    const auto sensor_5 = make_shared_pmr<Sensor>(Mrm::GetExtPmr(), "sensor_5");

    sensor_5->metadata.name = "Sensor 5";
    sensor_5->metadata.unit = "";
    sensor_5->metadata.description = "Test Sensor 5";

    sensor_5->configuration.type = SensorType::VIRTUAL_INDICATOR;
    sensor_5->configuration.sampling_rate_ms = 1000;
    sensor_5->configuration.expression = "sensor_1 < 400";

    const auto sensor_6 = make_shared_pmr<Sensor>(Mrm::GetExtPmr(), "sensor_6");

    sensor_6->metadata.name = "Sensor 6";
    sensor_6->metadata.unit = "";
    sensor_6->metadata.description = "Test Sensor 6";

    sensor_6->configuration.type = SensorType::CANBUS_ANALOG;
    sensor_6->configuration.canbus_source = make_unique_pmr<CanbusSource>(Mrm::GetExtPmr(), 1, 790, "RPM");

    const auto sensor_7 = make_shared_pmr<Sensor>(Mrm::GetExtPmr(), "sensor_7");

    sensor_7->metadata.name = "Sensor 7";
    sensor_7->metadata.unit = "";
    sensor_7->metadata.description = "Test Sensor 7";

    sensor_7->configuration.type = SensorType::CANBUS_RAW;
    sensor_7->configuration.canbus_source = make_unique_pmr<CanbusSource>(Mrm::GetExtPmr(), 0, 790);

    const auto sensor_8 = make_shared_pmr<Sensor>(Mrm::GetExtPmr(), "sensor_8");

    sensor_8->metadata.name = "Sensor 8";
    sensor_8->metadata.unit = "";
    sensor_8->metadata.description = "Test Sensor 8";

    sensor_8->configuration.type = SensorType::USER_ANALOG;
    sensor_8->configuration.script_path = "scripts/sensor_8.lua";
    sensor_8->configuration.sampling_rate_ms = 500;
    sensor_8->configuration.expression = "x * 2";

    const std::vector<std::shared_ptr<Sensor>> sensors = { sensor_1, sensor_2, sensor_3, sensor_4, sensor_5, sensor_6,
        sensor_7, sensor_8 };

    // std::vector<std::shared_ptr<Sensor>> sensors;

    // for(int i = 0; i < 50; i++) {
    //     auto sensor = make_shared_pmr<Sensor>(Mrm::GetExtPmr(), "sensor_" + std::to_string(i));

    //     sensor->metadata.name = "Sensor " + std::to_string(i);
    //     sensor->metadata.unit = "km/h";
    //     sensor->metadata.description = "Test Sensor " + std::to_string(i);

    //     sensor->configuration.type = SensorType::PHYSICAL_ANALOG;
    //     sensor->configuration.channel = 0;
    //     // sensor->configuration.script_path = "scripts/sensor_1.lua";
    //     sensor->configuration.sampling_rate_ms = 50;
    //     sensor->configuration.interpolation_method = InterpolationMethod::LINEAR;
    //     sensor->configuration.calibration_table = { {0.0, 0.0}, {5.0, 100.0} };
    //     // sensor->configuration.expression = "x * 2 + 1";

    //     sensors.push_back(sensor);
    // }

    sensors_configuration_manager->Update(sensors);
}

void SetupLoggingConfiguration(
    const std::shared_ptr<SensorsConfigurationManager>& sensors_configuration_manager,
    const std::shared_ptr<LoggingConfigurationManager>& logging_configuration_manager
) {

    const auto sensors = sensors_configuration_manager->Get();

    // It indexes the test sensors; a unit whose stored sensors configuration did not decode has none.
    if(sensors == nullptr || sensors->size() < 7) {
        LOG_ERR("The test logging configuration needs the test sensors (SetupTestSensors).");
        return;
    }

    auto logging_configuration = make_unique_pmr<LoggingConfiguration>(Mrm::GetExtPmr());
    logging_configuration->logging_interval_ms = 100;

    const SensorLoggingConfiguration sensor_logging_config_1 = {
        .is_enabled = true, .log_raw_value = false, .log_only_new_data = false
    };
    logging_configuration->sensor_configurations.insert({ sensors->at(0)->id_hash, sensor_logging_config_1 });

    const SensorLoggingConfiguration sensor_logging_config_2 = {
        .is_enabled = true, .log_raw_value = true, .log_only_new_data = false
    };
    logging_configuration->sensor_configurations.insert({ sensors->at(1)->id_hash, sensor_logging_config_2 });

    const SensorLoggingConfiguration sensor_logging_config_6 = {
        .is_enabled = true, .log_raw_value = true, .log_only_new_data = false
    };
    logging_configuration->sensor_configurations.insert({ sensors->at(5)->id_hash, sensor_logging_config_6 });

    const SensorLoggingConfiguration sensor_logging_config_7 = {
        .is_enabled = true, .log_raw_value = true, .log_only_new_data = true
    };
    logging_configuration->sensor_configurations.insert({ sensors->at(6)->id_hash, sensor_logging_config_7 });

    logging_configuration_manager->Update(*logging_configuration);
}
