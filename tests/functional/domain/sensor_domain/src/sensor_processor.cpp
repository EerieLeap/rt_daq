#include <zephyr/ztest.h>
#include <eerie_memory.hpp>

#include "utilities/memory/memory_resource_manager.h"
#include "utilities/guid/guid_generator.h"
#include "utilities/string/string_helpers.h"
#include "subsys/math_parser/expression_evaluator.h"
#include "subsys/time/rtc_provider.h"
#include "subsys/time/boot_elapsed_time_provider.h"
#include "subsys/time/time_service.h"

#include "subsys/adc/models/adc_configuration.h"
#include "subsys/adc/i_adc.h"
#include "subsys/adc/adc_simulator.h"
#include "subsys/gpio/i_gpio.h"
#include "subsys/gpio/gpio_simulator.h"
#include "subsys/fs/services/fs_service.h"

#include "subsys/device_tree/dt_fs.h"
#include "domain/sensor_domain/utilities/sensors_order_resolver.h"
#include "domain/sensor_domain/utilities/sensor_readings_frame.hpp"
#include "domain/sensor_domain/models/sensor.h"
#include "domain/sensor_domain/models/reading_status.h"
#include "domain/sensor_domain/sensor_readers/i_sensor_reader.h"
#include "domain/sensor_domain/sensor_readers/sensor_reader_physical_analog.h"
#include "domain/sensor_domain/sensor_readers/sensor_reader_physical_indicator.h"
#include "domain/sensor_domain/sensor_readers/sensor_reader_virtual_analog.h"
#include "domain/sensor_domain/sensor_readers/sensor_reader_virtual_indicator.h"
#include "domain/sensor_domain/processors/expression_processor.h"

#include "utilities/voltage_interpolator/linear_voltage_interpolator.hpp"
#include "utilities/voltage_interpolator/cubic_spline_voltage_interpolator.hpp"

using namespace eerie_memory;
using namespace eerie_leap::utilities::memory;
using namespace eerie_leap::utilities::guid;
using namespace eerie_leap::utilities::string;
using namespace eerie_leap::utilities::voltage_interpolator;

using namespace eerie_leap::configuration::services;
using namespace eerie_leap::configuration::json::configs;

using namespace eerie_leap::subsys::device_tree;
using namespace eerie_leap::subsys::adc;
using namespace eerie_leap::subsys::adc::models;
using namespace eerie_leap::subsys::gpio;
using namespace eerie_leap::subsys::time;
using namespace eerie_leap::subsys::math_parser;
using namespace eerie_leap::subsys::fs::services;

using namespace eerie_leap::domain::sensor_domain::processors;
using namespace eerie_leap::domain::sensor_domain::sensor_readers;

using namespace eerie_leap::domain::sensor_domain::models;
using namespace eerie_leap::domain::sensor_domain::utilities;

ZTEST_SUITE(sensor_processor, NULL, NULL, NULL, NULL, NULL);

std::vector<std::shared_ptr<Sensor>> sensor_processor_GetTestSensors() {
    std::pmr::vector<CalibrationData> calibration_data_1 {
        {0.0, 0.0},
        {3.3, 100.0}
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

    SensorsOrderResolver sensors_order_resolver;
    for(auto& sensor : sensors)
        sensors_order_resolver.AddSensor(sensor);

    return sensors_order_resolver.GetProcessingOrder();
}

AdcConfiguration sensor_processor_GetTestConfiguration() {
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

struct sensor_processor_HelperInstances {
    std::shared_ptr<SensorReadingsFrame> sensor_readings_frame;
    std::shared_ptr<std::vector<std::shared_ptr<ISensorReader>>> sensor_readers;
    std::vector<std::shared_ptr<Sensor>> sensors;
};

sensor_processor_HelperInstances sensor_processor_GetReadingInstances() {
    DtFs::InitInternalFs();
    auto fs_service = std::make_shared<FsService>(DtFs::GetInternalFsMp().value());

    fs_service->Format();

    auto time_provider = std::make_shared<BootElapsedTimeProvider>();
    auto rtc_provider = std::make_shared<RtcProvider>();
    auto time_service = std::make_shared<TimeService>(time_provider, rtc_provider);

    std::shared_ptr<GuidGenerator> guid_generator = std::make_shared<GuidGenerator>();

    auto cbor_adc_config_service = std::make_unique<CborConfigurationService<CborAdcConfig>>("adc_config", fs_service);
    auto json_adc_config_service = std::make_unique<JsonConfigurationService<JsonAdcConfig>>("adc_config", fs_service);

    AdcFactory adc_factory(nullptr);
    auto adc_manager = adc_factory.Create();
    adc_manager->Initialize();

    auto adc_configuration_manager = std::make_shared<AdcConfigurationManager>(
        std::move(cbor_adc_config_service), std::move(json_adc_config_service), adc_manager);

    const auto adc_configuration = sensor_processor_GetTestConfiguration();
    adc_configuration_manager->Update(adc_configuration);

    auto gpio = std::make_shared<GpioSimulator>();
    gpio->Initialize();

    auto sensor_readings_frame = make_shared_pmr<SensorReadingsFrame>(Mrm::GetDefaultPmr());
    auto sensors = sensor_processor_GetTestSensors();

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

    return sensor_processor_HelperInstances {
        .sensor_readings_frame = sensor_readings_frame,
        .sensor_readers = sensor_readers,
        .sensors = sensors
    };
}

class ReadingProcessedProcessor : public IReadingProcessor {
private:
    std::shared_ptr<SensorReadingsFrame> sensor_readings_frame_;

public:
    explicit ReadingProcessedProcessor(std::shared_ptr<SensorReadingsFrame> sensor_readings_frame)
        : sensor_readings_frame_(std::move(sensor_readings_frame)) {}

    void ProcessReading(const size_t sensor_id_hash) override {
        if(!sensor_readings_frame_->HasReading(sensor_id_hash))
            return;

        auto reading_opt = sensor_readings_frame_->TryGetReading(sensor_id_hash);
        if(!reading_opt.has_value())
            return;

        auto reading = reading_opt.value();

        reading.status = ReadingStatus::PROCESSED;
        sensor_readings_frame_->AddOrUpdateReading(reading);
    }
};

ZTEST(sensor_processor, test_ProcessReading) {
    auto helper = sensor_processor_GetReadingInstances();

    auto sensor_readings_frame = helper.sensor_readings_frame;
    auto sensor_readers = helper.sensor_readers;
    auto sensors = helper.sensors;

    for(int i = 0; i < sensor_readers->size(); i++)
        sensor_readers->at(i)->Read();

    auto reading_2_opt = sensor_readings_frame->TryGetReading("sensor_2");
    zassert_true(reading_2_opt.has_value());
    auto& reading_2 = reading_2_opt.value();

    auto reading_1_opt = sensor_readings_frame->TryGetReading("sensor_1");
    zassert_true(reading_1_opt.has_value());
    auto& reading_1 = reading_1_opt.value();

    for(auto& sensor : sensors) {
        if(sensor->configuration.expression_evaluator != nullptr) {
            sensor->configuration.expression_evaluator->RegisterVariableValueHandler(
                [&sensor_readings_frame](const std::string& sensor_id) {
                    return sensor_readings_frame->GetReadingValuePtr(sensor_id);
                });
        }
    }

    float reading_1_value = reading_1.value.value();
    float reading_2_value = reading_2.value.value();

    std::vector<std::shared_ptr<IReadingProcessor>> reading_processors;
    reading_processors.push_back(std::make_shared<ExpressionProcessor>(sensor_readings_frame));
    reading_processors.push_back(std::make_shared<ReadingProcessedProcessor>(sensor_readings_frame));

    for(auto& sensor : sensors) {
        for(auto& reading_processor : reading_processors)
            reading_processor->ProcessReading(sensor->id_hash);
    }

    auto proc_reading_2_opt = sensor_readings_frame->TryGetReading("sensor_2");
    zassert_true(proc_reading_2_opt.has_value());
    auto& proc_reading_2 = proc_reading_2_opt.value();
    zassert_equal(proc_reading_2.status, ReadingStatus::PROCESSED);
    zassert_true(proc_reading_2.value.has_value());
    float proc_reading_2_value = reading_2_value * 4 + 1.6;
    zassert_equal(proc_reading_2.value.value(), proc_reading_2_value);

    auto proc_reading_1_opt = sensor_readings_frame->TryGetReading("sensor_1");
    zassert_true(proc_reading_1_opt.has_value());
    auto& proc_reading_1 = proc_reading_1_opt.value();
    zassert_equal(proc_reading_1.status, ReadingStatus::PROCESSED);
    zassert_true(proc_reading_1.value.has_value());
    float proc_reading_1_value = reading_1_value * 2 + proc_reading_2_value + 1;
    zassert_equal(proc_reading_1.value.value(), proc_reading_1_value);

    auto proc_reading_3_opt = sensor_readings_frame->TryGetReading("sensor_3");
    zassert_true(proc_reading_3_opt.has_value());
    auto& proc_reading_3 = proc_reading_3_opt.value();
    zassert_equal(proc_reading_3.status, ReadingStatus::PROCESSED);
    zassert_true(proc_reading_3.value.has_value());
    float proc_reading_3_value = proc_reading_1_value + 8.34;
    zassert_equal(proc_reading_3.value.value(), proc_reading_3_value);

    auto proc_reading_4_opt = sensor_readings_frame->TryGetReading("sensor_4");
    zassert_true(proc_reading_4_opt.has_value());
    auto& proc_reading_4 = proc_reading_4_opt.value();
    zassert_equal(proc_reading_4.status, ReadingStatus::PROCESSED);
    zassert_true(proc_reading_4.value.has_value());
    zassert_true(proc_reading_4.value.value() == 1 || proc_reading_4.value.value() == 0);

    auto proc_reading_5_opt = sensor_readings_frame->TryGetReading("sensor_5");
    zassert_true(proc_reading_5_opt.has_value());
    auto& proc_reading_5 = proc_reading_5_opt.value();
    zassert_equal(proc_reading_5.status, ReadingStatus::PROCESSED);
    zassert_true(proc_reading_5.value.has_value());
    zassert_true(proc_reading_5.value.value() ==  proc_reading_1_value < 400);
}
