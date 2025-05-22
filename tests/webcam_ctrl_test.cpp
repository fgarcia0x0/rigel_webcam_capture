#include <rwc/core/webcam_device.hpp>
#include <rwc/core/webcam_manager.h>
#include <rwc/core/webcam_utils.h>

#include <memory>
#include <array>
#include <print>
#include <numeric>
#include <utility>

#include <gtest/gtest.h>

class webcam_ctrl_test : public ::testing::Test 
{
protected:
    static inline std::shared_ptr<rwc::webcam_device> wcam = {};

    static void SetUpTestSuite() 
    {
        wcam = rwc::webcam_manager::create_device();
        ASSERT_TRUE(wcam);
        ASSERT_EQ(wcam->open(), rwc::webcam_error_status::ok);
        ASSERT_TRUE(wcam->ctrl());
    }

    static void TearDownTestSuite() 
    {
        wcam.reset();
    }

    static void check_webcam_available()
    {
        ASSERT_TRUE(wcam);
        ASSERT_TRUE(wcam->is_opened());
        ASSERT_TRUE(wcam->ctrl());
    }

    static auto webcam_extract_all_properties()
    {
        std::vector<rwc::webcam_ctrl_property> properties;

        for (uint32_t index{}; index < std::to_underlying(rwc::webcam_property_type::last); ++index)
        {
            rwc::webcam_property_type prop_type{ index };

            // check this bug
            if (prop_type == rwc::webcam_property_type::hue || prop_type == rwc::webcam_property_type::power_line_freq)
                continue;

            auto property = wcam->ctrl()->read_property(prop_type);
            if (property.has_value())
                properties.push_back(std::move(property).value());
        }

        return properties;
    }

    static void webcam_assert_property_equals(rwc::webcam_property_type prop_type, int32_t new_value)
    {
        // read back and check
        auto new_property = wcam->ctrl()->read_property(prop_type);
        ASSERT_TRUE(new_property.has_value());
        ASSERT_EQ(new_property->value, new_value);
    }

    static void webcam_assert_property_is_default(rwc::webcam_property_type prop_type) 
    {
        SCOPED_TRACE("checking property: " + rwc::webcam_utils::prop_type_to_string(prop_type));
        auto property = wcam->ctrl()->read_property(prop_type);
        if (property.has_value())
            ASSERT_EQ(property->value, property->default_value);
    }

};

static constexpr std::array properties_to_check {
    rwc::webcam_property_type::brightness,
    rwc::webcam_property_type::contrast,
    rwc::webcam_property_type::saturation,
    rwc::webcam_property_type::sharpness,
    rwc::webcam_property_type::gamma,
    rwc::webcam_property_type::hue,
    rwc::webcam_property_type::white_balance,
    rwc::webcam_property_type::exposure,
    rwc::webcam_property_type::power_line_freq
};

static constexpr std::array auto_properties_to_check {
    rwc::webcam_property_type::auto_exposure,
    rwc::webcam_property_type::auto_focus,
    rwc::webcam_property_type::auto_gain,
    rwc::webcam_property_type::auto_white_balance
};

TEST_F(webcam_ctrl_test, start_webcam_device)
{
    wcam = rwc::webcam_manager::create_device();
    ASSERT_TRUE(wcam);
    ASSERT_EQ(wcam->open(), rwc::webcam_error_status::ok);
    ASSERT_TRUE(wcam->ctrl());
}

TEST_F(webcam_ctrl_test, read_basic_properties)
{
    check_webcam_available();

    for (const auto& prop : properties_to_check)
    {
        auto prop_value = wcam->ctrl()->read_property(prop);
        ASSERT_TRUE(prop_value != std::nullopt);
    }
}

TEST_F(webcam_ctrl_test, read_properties_check_range)
{
    check_webcam_available();

    for (const auto& prop : properties_to_check)
    {
        SCOPED_TRACE("checking property: " + rwc::webcam_utils::prop_type_to_string(prop));

        auto property = wcam->ctrl()->read_property(prop);
        ASSERT_TRUE(property.has_value());

        // dont analyze auto types here
        if (property->is_auto)
            continue;

        EXPECT_GE(property->value, property->minimum);
        EXPECT_LE(property->value, property->maximum);
    }
}

TEST_F(webcam_ctrl_test, read_properties_check_step_value)
{
    check_webcam_available();

    for (const auto& prop : properties_to_check)
    {
        SCOPED_TRACE("checking property: " + rwc::webcam_utils::prop_type_to_string(prop));

        auto property = wcam->ctrl()->read_property(prop);
        ASSERT_TRUE(property.has_value());

        // dont analyze auto types here
        if (property->is_auto)
            continue;

        // Validate step
        const auto range = property->maximum - property->minimum;
        EXPECT_GT(property->step, 0) << "step must be positive";
        EXPECT_EQ(range % property->step, 0) << "range not evenly divisible by step";
    }
}

TEST_F(webcam_ctrl_test, read_properties_check_default_value)
{
    check_webcam_available();

    for (const auto& prop : properties_to_check)
    {
        SCOPED_TRACE("checking property: " + rwc::webcam_utils::prop_type_to_string(prop));

        auto property = wcam->ctrl()->read_property(prop);
        ASSERT_TRUE(property.has_value());

        // dont analyze auto types here
        if (property->is_auto)
            continue;

        EXPECT_GE(property->default_value, property->minimum) << "default value below minimum";
        EXPECT_LE(property->default_value, property->maximum) << "default value above maximum";
        EXPECT_EQ((property->default_value - property->minimum) % property->step, 0) 
                 << "default value is not aligned to step";
    }
}

TEST_F(webcam_ctrl_test, read_auto_properties)
{
    check_webcam_available();

    for (const auto& prop : auto_properties_to_check)
    {
        SCOPED_TRACE("checking property: " + rwc::webcam_utils::prop_type_to_string(prop));
        if (auto property = wcam->ctrl()->read_property(prop); property)
        {
            EXPECT_TRUE(property->is_auto);
        }
    }
}

TEST_F(webcam_ctrl_test, write_basic_properties)
{
    check_webcam_available();

    for (const auto& prop : properties_to_check)
    {
        // check this bug
        if (prop == rwc::webcam_property_type::hue || prop == rwc::webcam_property_type::power_line_freq)
            continue;

        SCOPED_TRACE("checking property: " + rwc::webcam_utils::prop_type_to_string(prop));

        auto property = wcam->ctrl()->read_property(prop);
        ASSERT_TRUE(property.has_value());

        if (property->is_auto)
            continue;

        // setup values
        auto new_value = std::midpoint(property->minimum, property->maximum);
        auto old_value = property->value;
        
        // write value to control
        ASSERT_TRUE(wcam->ctrl()->write_property(prop, new_value));

        // read back and check new value
        webcam_assert_property_equals(prop, new_value);
        ASSERT_TRUE(wcam->ctrl()->write_property(prop, old_value));
        webcam_assert_property_equals(prop, old_value);
    }
}

TEST_F(webcam_ctrl_test, write_default_properties)
{
    check_webcam_available();

    for (const auto& prop : properties_to_check)
    {
        // check this bug
        if (prop == rwc::webcam_property_type::hue || prop == rwc::webcam_property_type::power_line_freq)
            continue;

        SCOPED_TRACE("checking property: " + rwc::webcam_utils::prop_type_to_string(prop));

        auto property = wcam->ctrl()->read_property(prop);
        ASSERT_TRUE(property.has_value());

        if (property->is_auto)
            continue;

        // setup values
        auto old_value = property->value;
        auto default_value = property->default_value;

        // write value to control
        ASSERT_TRUE(wcam->ctrl()->write_property_default(prop));

        // read back and check if value == default_value
        webcam_assert_property_equals(prop, default_value);

        // restore the value to old value
        ASSERT_TRUE(wcam->ctrl()->write_property(prop, old_value));
        webcam_assert_property_equals(prop, old_value);
    }
}

TEST_F(webcam_ctrl_test, reset_properties)
{
    check_webcam_available();

    // extract all properties
    std::vector<rwc::webcam_ctrl_property> original_properties = webcam_extract_all_properties();

    // modify all props to default value
    wcam->ctrl()->reset_properties();

    // check if property is equals his defaults
    for (const auto& property : original_properties)
        webcam_assert_property_is_default(property.type);

    // restore properties
    for (const auto& property : original_properties)
    {
        ASSERT_TRUE(wcam->ctrl()->write_property(property.type, property.value));
        webcam_assert_property_equals(property.type, property.value);
    }
}

int main(int argc, char** argv) 
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
