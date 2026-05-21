#include <memory>
#include <zephyr/logging/log.h>
#include <eerie_memory.hpp>

#include "utilities/memory/memory_resource_manager.h"
#include "utilities/dev_tools/system_info.h"
#include "utilities/guid/guid_generator.h"

#include "subsys/device_tree/dt_configurator.h"
#include "subsys/device_tree/dt_adc.h"
#include "subsys/device_tree/dt_gpio.h"
#include "subsys/device_tree/dt_fs.h"
#include "subsys/device_tree/dt_display.h"
#include "subsys/device_tree/dt_canbus.h"

#include "subsys/fs/services/fs_service.h"
#include "subsys/fs/services/sdmmc_service.h"
#include "subsys/gpio/gpio_factory.hpp"
#include "subsys/cfb/cfb.h"
#include "subsys/time/time_service.h"
#include "subsys/time/rtc_provider.h"
#include "subsys/time/boot_elapsed_time_provider.h"

#include "configuration/services/cbor_configuration_service.h"
#include "configuration/services/json_configuration_service.h"

#include "domain/system_domain/configuration/system_configuration_manager.h"
#include "domain/sensor_domain/configuration/adc_configuration_manager.h"
#include "domain/sensor_domain/configuration/sensors_configuration_manager.h"
#include "domain/logging_domain/configuration/logging_configuration_manager.h"
#include "domain/canbus_domain/configuration/canbus_configuration_manager.h"
#include "domain/sensor_domain/sensor_readers/sensor_reader_factory.h"
#include "domain/sensor_domain/isr_sensor_readers/isr_sensor_reader_factory.h"
#include "domain/sensor_domain/services/sensors_processing_service.h"
#include "domain/sensor_domain/services/calibration_service.h"
#include "domain/canbus_domain/services/canbus_service.h"
#include "domain/canbus_domain/services/canbus_scheduler_service.h"
#include "domain/canbus_com_domain/services/canbus_com_service.h"

#include "domain/logging_domain/services/log_writer_service.h"

#include "controllers/logging_controller.h"
#include "controllers/display_controller.h"

// Test sensors includes
#include "subsys/math_parser/expression_evaluator.h"
#include "domain/sensor_domain/models/sensor.h"

#include "utilities/voltage_interpolator/calibration_data.h"
#include "utilities/voltage_interpolator/linear_voltage_interpolator.hpp"
#include "utilities/voltage_interpolator/cubic_spline_voltage_interpolator.hpp"

using namespace eerie_leap::utilities::voltage_interpolator;
using namespace eerie_leap::domain::sensor_domain::configuration;
using namespace eerie_leap::domain::sensor_domain::models;
// End test sensors

using namespace eerie_memory;
using namespace eerie_leap::utilities::memory;
using namespace eerie_leap::utilities::dev_tools;
using namespace eerie_leap::utilities::guid;

using namespace eerie_leap::subsys::math_parser;
using namespace eerie_leap::subsys::device_tree;
using namespace eerie_leap::subsys::fs::services;
using namespace eerie_leap::subsys::gpio;
using namespace eerie_leap::subsys::cfb;
using namespace eerie_leap::subsys::time;
using namespace eerie_leap::subsys::canbus;
using namespace eerie_leap::subsys::adc;
using namespace eerie_leap::subsys::adc::utilities;

using namespace eerie_leap::configuration::json::configs;
using namespace eerie_leap::configuration::services;

using namespace eerie_leap::domain::canbus_domain::models;
using namespace eerie_leap::domain::canbus_domain::services;
using namespace eerie_leap::domain::canbus_domain::configuration;
using namespace eerie_leap::domain::system_domain::configuration;
using namespace eerie_leap::domain::sensor_domain::models;
using namespace eerie_leap::domain::sensor_domain::configuration;
using namespace eerie_leap::domain::sensor_domain::services;
using namespace eerie_leap::domain::sensor_domain::sensor_readers;
using namespace eerie_leap::domain::sensor_domain::isr_sensor_readers;
using namespace eerie_leap::domain::logging_domain::models;
using namespace eerie_leap::domain::logging_domain::services;
using namespace eerie_leap::domain::logging_domain::configuration;
using namespace eerie_leap::domain::canbus_com_domain::services;

using namespace eerie_leap::controllers;

LOG_MODULE_REGISTER(main_logger);

constexpr uint32_t SLEEP_TIME_MS = 10000;

void SetupSystemConfiguration(std::shared_ptr<SystemConfigurationManager> system_configuration_manager);
void SetupCanbusConfiguration(std::shared_ptr<CanbusConfigurationManager> canbus_configuration_manager);
void SetupAdcConfiguration(std::shared_ptr<AdcConfigurationManager> adc_configuration_manager);
void SetupTestSensors(std::shared_ptr<SensorsConfigurationManager> sensors_configuration_manager);
void SetupLoggingConfiguration(std::shared_ptr<SensorsConfigurationManager> sensors_configuration_manager,
    std::shared_ptr<LoggingConfigurationManager> logging_configuration_manager);

// NOTE: ADC reading timing for ESP32S3:
// Each sample adds up to about 25us to the reading time

// NOTE: Sensor reading timing for ESP32S3:
// ADC reading with 40 samples and no expression evaluation takes around 1200us
// ADC reading with 40 samples and expression evaluation takes minimum of 3300us
// GPIO reading with no expression evaluation takes around 130us
// GPIO reading with expression evaluation takes minimum of 1900us
// Formula evaluation takes minimum of 1800us, applicable to virtual sensors

int main(void) {
    DtConfigurator::Initialize();

    std::shared_ptr<Cfb> cfb = nullptr;

    if(DtDisplay::Get() != nullptr) {
        cfb = std::make_shared<Cfb>(DtDisplay::Get());

        if(!cfb->Initialize()) {
            LOG_ERR("Failed to initialize CFB.");
            return -1;
        }
    }

    auto display_controller = std::make_shared<DisplayController>(cfb);
    // NOTE: Affects performance, in particular occasionally adds around 5ms delay
    // to the sensor processing time. As it's running on a thread with same priority
    // as the CAN and sensor processing threads.
    display_controller->Initialize();

    std::shared_ptr<SdmmcService> sd_fs_service = nullptr;
    auto sd_fs_mp = DtFs::GetSdFsMp();
    if(sd_fs_mp.has_value()) {
        sd_fs_service = std::make_shared<SdmmcService>(sd_fs_mp.value(), DtFs::GetSdDiskName());
        sd_fs_service->RegisterIsSdCardPresentHandler(DtFs::IsSdCardPresent);
        if(!sd_fs_service->Initialize()) {
            LOG_ERR("Failed to initialize SD File System.");
        }

        // TODO: SPI SD card detection causes card to be
        // constatly mounted/umounted during logging
        // sd_fs_service->SdMonitorStart();
    }

    // NOTE: Don't use for WiFi supporting boards as WiFi is broken in Zephyr 4.1 and has memory allocation issues
    // At least on ESP32S3, it does connect if Zephyr revision is set to "main", but heap allocations cannot be moved
    // to the external RAM (e.g. PSRAM)

    // NOTE: Wifi thread seems to run out of memory on ESP32S3 after random period of time
#ifdef CONFIG_WIFI
    WifiApService::Initialize();
#endif // CONFIG_WIFI

    // The HTTP Server started here, but objects it uses are initialized later
    // This is done to prevent memory allocation issues on boards with WiFi support
    // at least on the ESP32S3 otherwise it struggles sharing memory between http_server_tid and conn_mgr_monitor threads
    // Or extra heap can be allocated to connection manager through CONFIG_NET_CONNECTION_MANAGER_MONITOR_STACK_SIZE
    // Which also helps with the issue
#ifdef CONFIG_NETWORKING
    HttpServer http_server;
    http_server.Start();
#endif // CONFIG_NETWORKING

    auto fs_service = std::make_shared<FsService>(DtFs::GetInternalFsMp().value());
    if(!fs_service->Initialize()) {
        LOG_ERR("Failed to initialize File System.");
        return -1;
    }

    auto rtc_provider = std::make_shared<RtcProvider>();
    auto boot_elapsed_time_provider = std::make_shared<BootElapsedTimeProvider>();
    auto time_service = std::make_shared<TimeService>(rtc_provider, boot_elapsed_time_provider);
    time_service->Initialize();

    auto guid_generator = std::make_shared<GuidGenerator>();

    auto cbor_system_config_service = std::make_unique<CborConfigurationService<CborSystemConfig>>(
        "system_config", fs_service);
    auto cbor_adc_config_service = std::make_unique<CborConfigurationService<CborAdcConfig>>(
        "adc_config", fs_service);
    auto cbor_sensors_config_service = std::make_unique<CborConfigurationService<CborSensorsConfig>>(
        "sensors_config", fs_service);
    auto cbor_canbus_config_service = std::make_unique<CborConfigurationService<CborCanbusConfig>>(
        "canbus_config", fs_service);

    auto json_adc_config_service = std::make_unique<JsonConfigurationService<JsonAdcConfig>>(
        "adc_config", sd_fs_service);
    auto json_sensors_config_service = std::make_unique<JsonConfigurationService<JsonSensorsConfig>>(
        "sensors_config", sd_fs_service);
    auto json_canbus_config_service = std::make_unique<JsonConfigurationService<JsonCanbusConfig>>(
        "canbus_config", sd_fs_service);

    AdcFactory adc_factory(DtAdc::Get);
    auto adc_manager = adc_factory.Create();
    adc_manager->Initialize();

    auto adc_configuration_manager = std::make_shared<AdcConfigurationManager>(
        std::move(cbor_adc_config_service), std::move(json_adc_config_service), adc_manager);

    // TODO: For test purposes only
    // SetupAdcConfiguration(adc_configuration_manager);

    GpioFactory gpio_factory(DtGpio::Get);
    auto gpio = gpio_factory.Create();
    gpio->Initialize();

    auto system_configuration_manager = std::make_shared<SystemConfigurationManager>(
        std::move(cbor_system_config_service));

    // TODO: For test purposes only
    // SetupSystemConfiguration(system_configuration_manager);

    auto canbus_configuration_manager = std::make_shared<CanbusConfigurationManager>(
        std::move(cbor_canbus_config_service), std::move(json_canbus_config_service), sd_fs_service);

    // TODO: For test purposes only
    SetupCanbusConfiguration(canbus_configuration_manager);

    auto sensors_configuration_manager = std::make_shared<SensorsConfigurationManager>(
        std::move(cbor_sensors_config_service),
        std::move(json_sensors_config_service),
        sd_fs_service,
        gpio->GetChannelCount(),
        adc_configuration_manager->Get()->GetChannelCount());

    auto canbus_service = std::make_shared<CanbusService>(DtCanbus::Get, canbus_configuration_manager);

    auto canbus_com_service = std::make_shared<CanbusComService>(canbus_service);
    canbus_com_service->Initialize();
    canbus_com_service->Start();

    // TODO: For test purposes only
    SetupTestSensors(sensors_configuration_manager);

    auto sensor_readings_frame = std::make_shared<SensorReadingsFrame>();

    std::shared_ptr<LoggingController> logging_controller = nullptr;
    if(sd_fs_service != nullptr) {
        auto cbor_logging_config_service = std::make_unique<CborConfigurationService<CborLoggingConfig>>(
            "logging_config", fs_service);
        auto json_logging_config_service = std::make_unique<JsonConfigurationService<JsonLoggingConfig>>(
            "logging_config", sd_fs_service);

        auto logging_configuration_manager = std::make_shared<LoggingConfigurationManager>(
            std::move(cbor_logging_config_service), std::move(json_logging_config_service));

        // TODO: For test purposes only
        // SetupLoggingConfiguration(sensors_configuration_manager, logging_configuration_manager);

        auto log_writer_service = std::make_shared<LogWriterService>(
            sd_fs_service,
            logging_configuration_manager,
            time_service,
            sensor_readings_frame);
        log_writer_service->Initialize();

        logging_controller = std::make_shared<LoggingController>(
            log_writer_service,
            sensors_configuration_manager,
            logging_configuration_manager,
            canbus_com_service,
            display_controller);
    }

    auto isr_sensor_reader_factory = std::make_shared<IsrSensorReaderFactory>(
        time_service,
        guid_generator,
        sensor_readings_frame,
        canbus_service);

    auto sensor_reader_factory = std::make_shared<SensorReaderFactory>(
        time_service,
        guid_generator,
        gpio,
        adc_configuration_manager,
        sensor_readings_frame);

    auto sensors_processing_service = std::make_shared<SensorsProcessingService>(
        sensors_configuration_manager,
        sensor_readings_frame,
        isr_sensor_reader_factory,
        sensor_reader_factory);
    sensors_processing_service->Initialize();

    auto calibration_service = std::make_shared<CalibrationService>(
        time_service,
        guid_generator,
        adc_configuration_manager,
        sensors_processing_service);
    calibration_service->Initialize();

    auto canbus_scheduler_service = std::make_shared<CanbusSchedulerService>(
        canbus_configuration_manager,
        canbus_service,
        sensor_readings_frame);
    canbus_scheduler_service->Initialize();

#ifdef CONFIG_NETWORKING
    http_server.Initialize(
        system_configuration_manager,
        adc_configuration_manager,
        sensors_configuration_manager,
        sensors_processing_service);
#endif // CONFIG_NETWORKING

    sensors_processing_service->Start();
    canbus_scheduler_service->Start();

    // TODO: Remove this and integrate into http server
    // calibration_service->Start(1);
    // k_msleep(10000);
    // calibration_service->Stop();

    while(true) {
        // SystemInfo::PrintHeapInfo();
        // SystemInfo::PrintStackInfo();
        k_msleep(SLEEP_TIME_MS);
    }

    return 0;
}

void SetupSystemConfiguration(std::shared_ptr<SystemConfigurationManager> system_configuration_manager) {
    auto system_configuration = make_shared_pmr<SystemConfiguration>(Mrm::GetExtPmr());

    system_configuration_manager->Update(*system_configuration);
}

void SetupCanbusConfiguration(std::shared_ptr<CanbusConfigurationManager> canbus_configuration_manager) {
    auto canbus_configuration = make_shared_pmr<CanbusConfiguration>(Mrm::GetExtPmr());
    canbus_configuration->com_bus_channel = 0;

    // ============================================
    // CANBus 0
    // ============================================

    CanChannelConfiguration canbus_channel_configuration_0(std::allocator_arg, Mrm::GetExtPmr());
    canbus_channel_configuration_0.type = CanbusType::CLASSICAL_CAN;
    canbus_channel_configuration_0.is_extended_id = false;
    canbus_channel_configuration_0.bus_channel = 0;
    canbus_channel_configuration_0.bitrate = 1'000'000;
    // canbus_channel_configuration_0.data_bitrate = 2000000;
    // canbus_channel_configuration_0.dbc_file_path = "configuration/canbus_0.dbc";

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
        auto message_configuration = make_shared_pmr<CanMessageConfiguration>(Mrm::GetExtPmr());
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
        canbus_channel_configuration_0.bus_channel,
        std::move(canbus_channel_configuration_0));

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

    auto message_configuration_1_0 = make_shared_pmr<CanMessageConfiguration>(Mrm::GetExtPmr());
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
        canbus_channel_configuration_1.bus_channel,
        std::move(canbus_channel_configuration_1));

    canbus_configuration_manager->Update(*canbus_configuration);
}

void SetupAdcConfiguration(std::shared_ptr<AdcConfigurationManager> adc_configuration_manager) {
    std::pmr::vector<CalibrationData> adc_calibration_data_samples {
        {0.501, 0.469},
        {1.0, 0.968},
        {2.0, 1.970},
        {3.002, 2.98},
        {4.003, 4.01},
        {5.0, 5.0}
    };

    auto adc_calibration_data_samples_ptr = std::make_shared<std::pmr::vector<CalibrationData>>(adc_calibration_data_samples);
    auto adc_calibrator = std::make_shared<AdcCalibrator>(InterpolationMethod::LINEAR, adc_calibration_data_samples_ptr);

    std::vector<std::shared_ptr<AdcChannelConfiguration>> channel_configurations = {
        std::make_shared<AdcChannelConfiguration>(adc_calibrator),
        std::make_shared<AdcChannelConfiguration>(adc_calibrator),
        std::make_shared<AdcChannelConfiguration>(adc_calibrator),
        std::make_shared<AdcChannelConfiguration>(adc_calibrator),
        // std::make_shared<AdcChannelConfiguration>(adc_calibrator),
        // std::make_shared<AdcChannelConfiguration>(adc_calibrator),
        // std::make_shared<AdcChannelConfiguration>(adc_calibrator),
        // std::make_shared<AdcChannelConfiguration>(adc_calibrator),
    };

    auto adc_configuration = std::make_shared<AdcConfiguration>();
    adc_configuration->samples = 40;
    adc_configuration->channel_configurations =
        std::make_shared<std::vector<std::shared_ptr<AdcChannelConfiguration>>>(channel_configurations);

    adc_configuration_manager->Update(*adc_configuration.get());
}

void SetupTestSensors(std::shared_ptr<SensorsConfigurationManager> sensors_configuration_manager) {
    // Test Sensors

    std::pmr::vector<CalibrationData> calibration_data_1 {
        {0.0, 0.0},
        {5.0, 100.0}
    };
    auto calibration_data_1_ptr = std::make_shared<std::pmr::vector<CalibrationData>>(calibration_data_1);

    auto sensor_1 = make_shared_pmr<Sensor>(Mrm::GetExtPmr(), "sensor_1");

    sensor_1->metadata.name = "Sensor 1";
    sensor_1->metadata.unit = "km/h";
    sensor_1->metadata.description = "Test Sensor 1";

    sensor_1->configuration.type = SensorType::PHYSICAL_ANALOG;
    sensor_1->configuration.channel = 0;
    // sensor_1->configuration.script_path = "scripts/sensor_1.lua";
    sensor_1->configuration.sampling_rate_ms = 50;
    sensor_1->configuration.voltage_interpolator = make_unique_pmr<LinearVoltageInterpolator>(Mrm::GetExtPmr(), calibration_data_1_ptr);
    // sensor_1->configuration.expression_evaluator = make_unique_pmr<ExpressionEvaluator>(Mrm::GetExtPmr(), "x * 2 + sensor_2 + 1");

    std::pmr::vector<CalibrationData> calibration_data_2 {
        {0.0, 90.0},
        {5.0, 100.0},
    };
    auto calibration_data_2_ptr = std::make_shared<std::pmr::vector<CalibrationData>>(calibration_data_2);

    auto sensor_2 = make_shared_pmr<Sensor>(Mrm::GetExtPmr(), "sensor_2");

    sensor_2->metadata.name = "Sensor 2";
    sensor_2->metadata.unit = "km/h";
    sensor_2->metadata.description = "Test Sensor 2";

    sensor_2->configuration.type = SensorType::PHYSICAL_ANALOG;
    sensor_2->configuration.channel = 1;
    sensor_2->configuration.sampling_rate_ms = 1000;
    sensor_2->configuration.voltage_interpolator = make_unique_pmr<CubicSplineVoltageInterpolator>(Mrm::GetExtPmr(), calibration_data_2_ptr);
    sensor_2->configuration.expression_evaluator = make_unique_pmr<ExpressionEvaluator>(Mrm::GetExtPmr(), "x * 4 + 1.6");

    auto sensor_3 = make_shared_pmr<Sensor>(Mrm::GetExtPmr(), "sensor_3");

    sensor_3->metadata.name = "Sensor 3";
    sensor_3->metadata.unit = "km/h";
    sensor_3->metadata.description = "Test Sensor 3";

    sensor_3->configuration.type = SensorType::VIRTUAL_ANALOG;
    sensor_3->configuration.sampling_rate_ms = 2000;
    sensor_3->configuration.expression_evaluator = make_unique_pmr<ExpressionEvaluator>(Mrm::GetExtPmr(), "2 + 8.34");

    auto sensor_4 = make_shared_pmr<Sensor>(Mrm::GetExtPmr(), "sensor_4");

    sensor_4->metadata.name = "Sensor 4";
    sensor_4->metadata.unit = "";
    sensor_4->metadata.description = "Test Sensor 4";

    sensor_4->configuration.type = SensorType::PHYSICAL_INDICATOR;
    sensor_4->configuration.channel = 1;
    sensor_4->configuration.sampling_rate_ms = 1000;

    auto sensor_5 = make_shared_pmr<Sensor>(Mrm::GetExtPmr(), "sensor_5");

    sensor_5->metadata.name = "Sensor 5";
    sensor_5->metadata.unit = "";
    sensor_5->metadata.description = "Test Sensor 5";

    sensor_5->configuration.type = SensorType::VIRTUAL_INDICATOR;
    sensor_5->configuration.sampling_rate_ms = 1000;
    sensor_5->configuration.expression_evaluator = make_unique_pmr<ExpressionEvaluator>(Mrm::GetExtPmr(), "sensor_1 < 400");

    auto sensor_6 = make_shared_pmr<Sensor>(Mrm::GetExtPmr(), "sensor_6");

    sensor_6->metadata.name = "Sensor 6";
    sensor_6->metadata.unit = "";
    sensor_6->metadata.description = "Test Sensor 6";

    sensor_6->configuration.type = SensorType::CANBUS_ANALOG;
    sensor_6->configuration.sampling_rate_ms = 1000;
    sensor_6->configuration.canbus_source = make_unique_pmr<CanbusSource>(Mrm::GetExtPmr(), 1, 790, "RPM");

    auto sensor_7 = make_shared_pmr<Sensor>(Mrm::GetExtPmr(), "sensor_7");

    sensor_7->metadata.name = "Sensor 7";
    sensor_7->metadata.unit = "";
    sensor_7->metadata.description = "Test Sensor 7";

    sensor_7->configuration.type = SensorType::CANBUS_RAW;
    sensor_7->configuration.sampling_rate_ms = 300;
    sensor_7->configuration.canbus_source = make_unique_pmr<CanbusSource>(Mrm::GetExtPmr(), 0, 790);

    auto sensor_8 = make_shared_pmr<Sensor>(Mrm::GetExtPmr(), "sensor_8");

    sensor_8->metadata.name = "Sensor 8";
    sensor_8->metadata.unit = "";
    sensor_8->metadata.description = "Test Sensor 8";

    sensor_8->configuration.type = SensorType::USER_ANALOG;
    sensor_8->configuration.script_path = "scripts/sensor_8.lua";
    sensor_8->configuration.sampling_rate_ms = 500;
    sensor_8->configuration.expression_evaluator = make_unique_pmr<ExpressionEvaluator>(Mrm::GetExtPmr(), "x * 2");

    std::vector<std::shared_ptr<Sensor>> sensors = {
        sensor_1,
        sensor_2,
        sensor_3,
        sensor_4,
        sensor_5,
        sensor_6,
        sensor_7,
        sensor_8
    };

    // std::vector<std::shared_ptr<Sensor>> sensors;

    // for(int i = 0; i < 50; i++) {
    //     std::pmr::vector<CalibrationData> calibration_data_1 {
    //         {0.0, 0.0},
    //         {5.0, 100.0}
    //     };
    //     auto calibration_data_1_ptr = make_shared_pmr<std::pmr::vector<CalibrationData>>(Mrm::GetExtPmr(), calibration_data_1);

    //     auto sensor = make_shared_pmr<Sensor>(Mrm::GetExtPmr(), "sensor_" + std::to_string(i));

    //     sensor->metadata.name = "Sensor " + std::to_string(i);
    //     sensor->metadata.unit = "km/h";
    //     sensor->metadata.description = "Test Sensor " + std::to_string(i);

    //     sensor->configuration.type = SensorType::PHYSICAL_ANALOG;
    //     sensor->configuration.channel = 0;
    //     // sensor->configuration.script_path = "scripts/sensor_1.lua";
    //     sensor->configuration.sampling_rate_ms = 50;
    //     sensor->configuration.voltage_interpolator = make_unique_pmr<LinearVoltageInterpolator>(Mrm::GetExtPmr(), calibration_data_1_ptr);
    //     // sensor->configuration.expression_evaluator = make_unique_pmr<ExpressionEvaluator>(Mrm::GetExtPmr(), "x * 2 + 1");

    //     sensors.push_back(sensor);
    // }

    sensors_configuration_manager->Update(sensors);
}

void SetupLoggingConfiguration(std::shared_ptr<SensorsConfigurationManager> sensors_configuration_manager,
    std::shared_ptr<LoggingConfigurationManager> logging_configuration_manager) {

    auto sensors = sensors_configuration_manager->Get();

    auto logging_configuration = make_unique_pmr<LoggingConfiguration>(Mrm::GetExtPmr());
    logging_configuration->logging_interval_ms = 100;

    SensorLoggingConfiguration sensor_logging_config_1 = {
        .is_enabled = true,
        .log_raw_value = false,
        .log_only_new_data = false
    };
    logging_configuration->sensor_configurations.insert({sensors->at(0)->id_hash, sensor_logging_config_1});

    SensorLoggingConfiguration sensor_logging_config_2 = {
        .is_enabled = true,
        .log_raw_value = true,
        .log_only_new_data = false
    };
    logging_configuration->sensor_configurations.insert({sensors->at(1)->id_hash, sensor_logging_config_2});

    SensorLoggingConfiguration sensor_logging_config_6 = {
        .is_enabled = true,
        .log_raw_value = true,
        .log_only_new_data = false
    };
    logging_configuration->sensor_configurations.insert({sensors->at(5)->id_hash, sensor_logging_config_6});

    SensorLoggingConfiguration sensor_logging_config_7 = {
        .is_enabled = true,
        .log_raw_value = true,
        .log_only_new_data = true
    };
    logging_configuration->sensor_configurations.insert({sensors->at(6)->id_hash, sensor_logging_config_7});

    logging_configuration_manager->Update(*logging_configuration);
}
