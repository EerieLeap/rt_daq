#include <zephyr/ztest.h>
#include <eerie_memory.hpp>

#include "utilities/memory/memory_resource_manager.h"
#include "utilities/guid/guid_generator.h"
#include "utilities/string/string_helpers.h"

#include "configuration/cbor/cbor_adc_config/cbor_adc_config.h"
#include "configuration/services/cbor_configuration_service.h"

#include "subsys/device_tree/dt_fs.h"
#include "subsys/fs/services/fs_service.h"
#include "subsys/adc/models/adc_configuration.h"
#include "subsys/adc/i_adc.h"
#include "subsys/adc/adc_simulator.h"
#include "subsys/gpio/i_gpio.h"
#include "subsys/gpio/gpio_simulator.h"
#include "subsys/time/rtc_provider.h"
#include "subsys/time/boot_elapsed_time_provider.h"
#include "subsys/time/time_service.h"
#include "subsys/math_parser/expression_evaluator.h"

#include "domain/sensor_domain/configuration/adc_configuration_manager.h"
#include "domain/sensor_domain/utilities/sensor_readings_frame.hpp"
#include "domain/sensor_domain/sensor_readers/i_sensor_reader.h"
#include "domain/sensor_domain/sensor_readers/sensor_reader_physical_analog.h"
#include "domain/sensor_domain/sensor_readers/sensor_reader_physical_indicator.h"
#include "domain/sensor_domain/sensor_readers/sensor_reader_virtual_analog.h"
#include "domain/sensor_domain/sensor_readers/sensor_reader_virtual_indicator.h"

#include "domain/sensor_domain/models/sensor.h"
#include "domain/sensor_domain/models/reading_status.h"
#include "utilities/voltage_interpolator/linear_voltage_interpolator.hpp"
#include "utilities/voltage_interpolator/cubic_spline_voltage_interpolator.hpp"

using namespace eerie_memory;
using namespace eerie_leap::utilities::memory;
using namespace eerie_leap::utilities::guid;
using namespace eerie_leap::utilities::string;
using namespace eerie_leap::utilities::voltage_interpolator;

using namespace eerie_leap::configuration::services;

using namespace eerie_leap::subsys::device_tree;
using namespace eerie_leap::subsys::adc;
using namespace eerie_leap::subsys::adc::models;
using namespace eerie_leap::subsys::gpio;
using namespace eerie_leap::subsys::time;
using namespace eerie_leap::subsys::math_parser;
using namespace eerie_leap::subsys::fs::services;

using namespace eerie_leap::domain::sensor_domain::configuration;
using namespace eerie_leap::domain::sensor_domain::sensor_readers;

using namespace eerie_leap::domain::sensor_domain::models;

ZTEST_SUITE(sensors_reader, NULL, NULL, NULL, NULL, NULL);

std::vector<std::shared_ptr<Sensor>> sensors_reader_GetTestSensors() {
    std::pmr::vector<CalibrationData> calibration_data_1 {
        {0.0, 0.0},
        {3.3, 5.0}
    };
    auto calibration_data_1_ptr = std::make_shared<std::pmr::vector<CalibrationData>>(calibration_data_1);

    auto sensor_1 = std::make_shared<Sensor>(std::allocator_arg, Mrm::GetDefaultPmr(), "sensor_1");

    sensor_1->metadata.name = "Sensor 1";
    sensor_1->metadata.unit = "km/h";
    sensor_1->metadata.description = "Test Sensor 1";

    sensor_1->configuration.type = SensorType::PHYSICAL_ANALOG;
    sensor_1->configuration.channel = 0;
    sensor_1->configuration.sampling_rate_ms = 1000;
    sensor_1->configuration.voltage_interpolator = make_unique_pmr<LinearVoltageInterpolator>(Mrm::GetDefaultPmr(), calibration_data_1_ptr);
    sensor_1->configuration.expression_evaluator = make_unique_pmr<ExpressionEvaluator>(Mrm::GetDefaultPmr(), "x * 2 + sensor_2 + 1");

    std::pmr::vector<CalibrationData> calibration_data_2 {
        {0.0, 0.0},
        {1.0, 29.0},
        {2.0, 111.0},
        {2.5, 162.0},
        {3.3, 200.0}
    };
    auto calibration_data_2_ptr = std::make_shared<std::pmr::vector<CalibrationData>>(calibration_data_2);

    auto sensor_2 = std::make_shared<Sensor>(std::allocator_arg, Mrm::GetDefaultPmr(), "sensor_2");

    sensor_2->metadata.name = "Sensor 2";
    sensor_2->metadata.unit = "km/h";
    sensor_2->metadata.description = "Test Sensor 2";

    sensor_2->configuration.type = SensorType::PHYSICAL_ANALOG;
    sensor_2->configuration.channel = 1;
    sensor_2->configuration.sampling_rate_ms = 500;
    sensor_2->configuration.voltage_interpolator = make_unique_pmr<CubicSplineVoltageInterpolator>(Mrm::GetDefaultPmr(), calibration_data_2_ptr);
    sensor_2->configuration.expression_evaluator = make_unique_pmr<ExpressionEvaluator>(Mrm::GetDefaultPmr(), "x * 4 + 1.6");

    auto sensor_3 = std::make_shared<Sensor>(std::allocator_arg, Mrm::GetDefaultPmr(), "sensor_3");

    sensor_3->metadata.name = "Sensor 3";
    sensor_3->metadata.unit = "km/h";
    sensor_3->metadata.description = "Test Sensor 3";

    sensor_3->configuration.type = SensorType::VIRTUAL_ANALOG;
    sensor_3->configuration.channel = std::nullopt;
    sensor_3->configuration.sampling_rate_ms = 2000;
    sensor_3->configuration.expression_evaluator = make_unique_pmr<ExpressionEvaluator>(Mrm::GetDefaultPmr(), "sensor_1 + 8.34");

    auto sensor_4 = std::make_shared<Sensor>(std::allocator_arg, Mrm::GetDefaultPmr(), "sensor_4");

    sensor_4->metadata.name = "Sensor 4";
    sensor_4->metadata.unit = "";
    sensor_4->metadata.description = "Test Sensor 4";

    sensor_4->configuration.type = SensorType::PHYSICAL_INDICATOR;
    sensor_4->configuration.channel = 1;
    sensor_4->configuration.sampling_rate_ms = 1000;
    sensor_4->configuration.voltage_interpolator = make_unique_pmr<CubicSplineVoltageInterpolator>(Mrm::GetDefaultPmr(), calibration_data_2_ptr);

    auto sensor_5 = std::make_shared<Sensor>(std::allocator_arg, Mrm::GetDefaultPmr(), "sensor_5");

    sensor_5->metadata.name = "Sensor 5";
    sensor_5->metadata.unit = "";
    sensor_5->metadata.description = "Test Sensor 5";

    sensor_5->configuration.type = SensorType::VIRTUAL_INDICATOR;
    sensor_5->configuration.sampling_rate_ms = 1000;
    sensor_5->configuration.expression_evaluator = make_unique_pmr<ExpressionEvaluator>(Mrm::GetDefaultPmr(), "sensor_1 < 400");

    std::vector<std::shared_ptr<Sensor>> sensors = {
        sensor_1, sensor_2, sensor_3, sensor_4, sensor_5 };

    return sensors;
}

AdcConfiguration sensors_reader_GetTestConfiguration() {
    std::pmr::vector<CalibrationData> adc_calibration_data_samples {
        {0.0, 0.0},
        {5.0, 5.0}
    };

    auto adc_calibration_data_samples_ptr = std::make_shared<std::pmr::vector<CalibrationData>>(adc_calibration_data_samples);
    auto adc_calibrator = std::make_shared<AdcCalibrator>(InterpolationMethod::LINEAR, adc_calibration_data_samples_ptr);

    auto adc_channel_configuration = std::make_shared<AdcChannelConfiguration>(adc_calibrator);

    std::vector<std::shared_ptr<AdcChannelConfiguration>> channel_configurations;
    channel_configurations.reserve(8);
    for(int i = 0; i < 8; i++)
        channel_configurations.push_back(adc_channel_configuration);

    AdcConfiguration adc_configuration;
    adc_configuration.samples = 40;
    adc_configuration.channel_configurations =
        std::make_shared<std::vector<std::shared_ptr<AdcChannelConfiguration>>>(channel_configurations);

    return adc_configuration;
}

struct sensors_reader_HelperInstances {
    std::shared_ptr<SensorReadingsFrame> sensor_readings_frame;
    std::shared_ptr<std::vector<std::shared_ptr<ISensorReader>>> sensor_readers;
};

sensors_reader_HelperInstances sensors_reader_GetReadingInstances() {
    DtFs::InitInternalFs();
    auto fs_service = std::make_shared<FsService>(DtFs::GetInternalFsMp().value());

    fs_service->Format();

    auto time_provider = std::make_shared<BootElapsedTimeProvider>();
    auto rtc_provider = std::make_shared<RtcProvider>();
    auto time_service = std::make_shared<TimeService>(time_provider, rtc_provider);

    std::shared_ptr<GuidGenerator> guid_generator = std::make_shared<GuidGenerator>();

    const auto adc_configuration = sensors_reader_GetTestConfiguration();

    auto cbor_adc_config_service = std::make_unique<CborConfigurationService<CborAdcConfig>>("adc_config", fs_service);
    auto json_adc_config_service = std::make_unique<JsonConfigurationService<JsonAdcConfig>>("adc_config", fs_service);

    AdcFactory adc_factory(nullptr);
    auto adc_manager = adc_factory.Create();
    adc_manager->Initialize();

    auto adc_configuration_manager = std::make_shared<AdcConfigurationManager>(
        std::move(cbor_adc_config_service), std::move(json_adc_config_service), adc_manager);

    auto gpio = std::make_shared<GpioSimulator>();
    gpio->Initialize();

    auto sensor_readings_frame = make_shared_pmr<SensorReadingsFrame>(Mrm::GetDefaultPmr());
    auto sensors = sensors_reader_GetTestSensors();

    auto sensor_readers = std::make_shared<std::vector<std::shared_ptr<ISensorReader>>>();
    for(int i = 0; i < sensors.size(); i++) {
        std::shared_ptr<ISensorReader> sensor_reader;

        if(sensors[i]->configuration.type == SensorType::PHYSICAL_ANALOG) {
            sensor_reader = std::make_shared<SensorReaderPhysicalAnalog>(
                time_service,
                guid_generator,
                sensor_readings_frame,
                sensors[i],
                adc_configuration_manager);
        } else if(sensors[i]->configuration.type == SensorType::VIRTUAL_ANALOG) {
            sensor_reader = std::make_shared<SensorReaderVirtualAnalog>(
                time_service,
                guid_generator,
                sensor_readings_frame,
                sensors[i]);
        } else if(sensors[i]->configuration.type == SensorType::PHYSICAL_INDICATOR) {
            sensor_reader = std::make_shared<SensorReaderPhysicalIndicator>(
                time_service,
                guid_generator,
                sensor_readings_frame,
                sensors[i],
                gpio);
        } else if(sensors[i]->configuration.type == SensorType::VIRTUAL_INDICATOR) {
            sensor_reader = std::make_shared<SensorReaderVirtualIndicator>(
                time_service,
                guid_generator,
                sensor_readings_frame,
                sensors[i]);
        } else {
            throw std::runtime_error("Unsupported sensor type");
        }

        sensor_readers->push_back(sensor_reader);
    }

    return sensors_reader_HelperInstances {
        .sensor_readings_frame = sensor_readings_frame,
        .sensor_readers = sensor_readers
    };
}

ZTEST(sensors_reader, test_Read) {
    auto helper = sensors_reader_GetReadingInstances();

    auto sensor_readings_frame = helper.sensor_readings_frame;
    auto sensor_readers = helper.sensor_readers;

    std::vector<std::string> sensor_names {"sensor_1", "sensor_2", "sensor_3", "sensor_4", "sensor_5"};

    for(auto& sensor_name : sensor_names) {
        auto sensor_hash = StringHelpers::GetHash(sensor_name);
        zassert_false(sensor_readings_frame->HasReading(sensor_hash));
    }

    for(int i = 0; i < sensor_readers->size(); i++)
        sensor_readers->at(i)->Read();

    for(auto& sensor_name : sensor_names) {
        auto sensor_hash = StringHelpers::GetHash(sensor_name);
        zassert_true(sensor_readings_frame->HasReading(sensor_hash));
    }

    auto reading_2_opt = sensor_readings_frame->TryGetReading("sensor_2");
    zassert_true(reading_2_opt.has_value());
    auto& reading_2 = reading_2_opt.value();
    zassert_equal(reading_2.status, ReadingStatus::INTERPOLATED);
    zassert_true(reading_2.value.has_value());
    zassert_true(reading_2.timestamp.has_value());
    zassert_true(reading_2.id.AsUint64() > 0);
    zassert_between_inclusive(reading_2.value.value(), 0.0f, 200.0f);

    auto reading_1_opt = sensor_readings_frame->TryGetReading("sensor_1");
    zassert_true(reading_1_opt.has_value());
    auto& reading_1 = reading_1_opt.value();
    zassert_equal(reading_1.status, ReadingStatus::INTERPOLATED);
    zassert_true(reading_1.value.has_value());
    zassert_true(reading_1.timestamp.has_value());
    zassert_true(reading_1.id.AsUint64() > 0);
    zassert_between_inclusive(reading_1.value.value(), 0.0f, 5.0f);

    auto reading_3_opt = sensor_readings_frame->TryGetReading("sensor_3");
    zassert_true(reading_3_opt.has_value());
    auto& reading_3 = reading_3_opt.value();
    zassert_equal(reading_3.status, ReadingStatus::UNINITIALIZED);
    zassert_true(reading_3.timestamp.has_value());
    zassert_true(reading_3.id.AsUint64() > 0);
    zassert_false(reading_3.value.has_value());

    auto reading_4_opt = sensor_readings_frame->TryGetReading("sensor_4");
    zassert_true(reading_4_opt.has_value());
    auto& reading_4 = reading_4_opt.value();
    zassert_equal(reading_4.status, ReadingStatus::RAW);
    zassert_true(reading_4.timestamp.has_value());
    zassert_true(reading_4.id.AsUint64() > 0);
    zassert_true(reading_4.value.has_value());
    zassert_true(reading_4.value.value() == 1 || reading_4.value.value() == 0);

    auto reading_5_opt = sensor_readings_frame->TryGetReading("sensor_5");
    zassert_true(reading_5_opt.has_value());
    auto& reading_5 = reading_5_opt.value();
    zassert_equal(reading_5.status, ReadingStatus::UNINITIALIZED);
    zassert_true(reading_5.timestamp.has_value());
    zassert_true(reading_5.id.AsUint64() > 0);
    zassert_false(reading_5.value.has_value());
}
