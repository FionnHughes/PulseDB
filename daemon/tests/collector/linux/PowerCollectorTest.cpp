#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>

#include "PowerCollector.h"
#include "collector/MetricSnapshot.h"

namespace pulsedb {

    class PowerCollectorTest : public ::testing::Test {
    protected:
        void TearDown() override { std::filesystem::remove_all(base_path); }

        std::string base_path = "test_power_supply";

        // makes the supply folder too, each supply is its own folder
        void write_fixture(const std::string& path, const std::string& content) {
            std::filesystem::create_directories(std::filesystem::path(path).parent_path());
            std::ofstream out(path, std::ios::trunc);
            out << content;
        }

        std::string type_battery = "Battery\n";
        std::string type_mains = "Mains\n";
        std::string type_usb = "USB\n";

        std::string ac_online = "POWER_SUPPLY_NAME=AC\n"
                                "POWER_SUPPLY_ONLINE=1\n";
        std::string ac_offline = "POWER_SUPPLY_NAME=AC\n"
                                 "POWER_SUPPLY_ONLINE=0\n";
        std::string usbc_offline = "POWER_SUPPLY_NAME=ucsi-source-psy-USBC000:001\n"
                                   "POWER_SUPPLY_ONLINE=0\n";
        // negative power_now on purpose, some drivers report it like that
        std::string bat0_discharging = "POWER_SUPPLY_NAME=BAT0\n"
                                       "POWER_SUPPLY_STATUS=Discharging\n"
                                       "POWER_SUPPLY_PRESENT=1\n"
                                       "POWER_SUPPLY_CYCLE_COUNT=123\n"
                                       "POWER_SUPPLY_POWER_NOW=-10000000\n"
                                       "POWER_SUPPLY_ENERGY_FULL_DESIGN=62500000\n"
                                       "POWER_SUPPLY_ENERGY_FULL=50000000\n"
                                       "POWER_SUPPLY_ENERGY_NOW=40000000\n"
                                       "POWER_SUPPLY_CAPACITY=80\n";
        std::string bat0_charging = "POWER_SUPPLY_NAME=BAT0\n"
                                    "POWER_SUPPLY_STATUS=Charging\n"
                                    "POWER_SUPPLY_PRESENT=1\n"
                                    "POWER_SUPPLY_CYCLE_COUNT=123\n"
                                    "POWER_SUPPLY_POWER_NOW=15000000\n"
                                    "POWER_SUPPLY_ENERGY_FULL_DESIGN=62500000\n"
                                    "POWER_SUPPLY_ENERGY_FULL=50000000\n"
                                    "POWER_SUPPLY_ENERGY_NOW=40000000\n"
                                    "POWER_SUPPLY_CAPACITY=80\n";
        // wireless mouse, has to be skipped or percent / cycles / watts all change
        std::string mouse = "POWER_SUPPLY_NAME=hidpp_battery_0\n"
                            "POWER_SUPPLY_SCOPE=Device\n"
                            "POWER_SUPPLY_STATUS=Discharging\n"
                            "POWER_SUPPLY_PRESENT=1\n"
                            "POWER_SUPPLY_CYCLE_COUNT=999\n"
                            "POWER_SUPPLY_POWER_NOW=500000\n"
                            "POWER_SUPPLY_ENERGY_FULL=2000000\n"
                            "POWER_SUPPLY_ENERGY_NOW=1000000\n"
                            "POWER_SUPPLY_CAPACITY=50\n";
        // charge style batteries, capacity here should be ignored since theres two
        std::string bat0_charge_style = "POWER_SUPPLY_NAME=BAT0\n"
                                        "POWER_SUPPLY_STATUS=Discharging\n"
                                        "POWER_SUPPLY_PRESENT=1\n"
                                        "POWER_SUPPLY_CYCLE_COUNT=100\n"
                                        "POWER_SUPPLY_VOLTAGE_NOW=12000000\n"
                                        "POWER_SUPPLY_CURRENT_NOW=1000000\n"
                                        "POWER_SUPPLY_CHARGE_FULL_DESIGN=5000000\n"
                                        "POWER_SUPPLY_CHARGE_FULL=4000000\n"
                                        "POWER_SUPPLY_CHARGE_NOW=2000000\n"
                                        "POWER_SUPPLY_CAPACITY=50\n";
        std::string bat1_charge_style = "POWER_SUPPLY_NAME=BAT1\n"
                                        "POWER_SUPPLY_STATUS=Discharging\n"
                                        "POWER_SUPPLY_PRESENT=1\n"
                                        "POWER_SUPPLY_CYCLE_COUNT=250\n"
                                        "POWER_SUPPLY_VOLTAGE_NOW=12000000\n"
                                        "POWER_SUPPLY_CURRENT_NOW=1000000\n"
                                        "POWER_SUPPLY_CHARGE_FULL_DESIGN=5000000\n"
                                        "POWER_SUPPLY_CHARGE_FULL=4000000\n"
                                        "POWER_SUPPLY_CHARGE_NOW=3200000\n"
                                        "POWER_SUPPLY_CAPACITY=80\n";
    };

    // no supply folder, and an offline usb-c port, should be on ac with no battery
    TEST_F(PowerCollectorTest, DesktopReportsOnAc) {
        PowerCollector no_folder("invalid_dir", 1);
        MetricSnapshot snap;

        ASSERT_TRUE(no_folder.initialize());
        ASSERT_TRUE(no_folder.collect());
        no_folder.fill_snapshot(snap);

        EXPECT_TRUE(snap.power.on_ac_power);
        EXPECT_FALSE(snap.power.battery_present);
        EXPECT_EQ(snap.power.battery_percent, 255);
        EXPECT_EQ(snap.power.battery_seconds_remaining, -1);
        EXPECT_EQ(snap.power.battery_full_seconds_remaining, -1);

        write_fixture(base_path + "/ucsi-source-psy-USBC000:001/type", type_usb);
        write_fixture(base_path + "/ucsi-source-psy-USBC000:001/uevent", usbc_offline);

        PowerCollector usbc_desktop(base_path, 1);
        ASSERT_TRUE(usbc_desktop.initialize());
        usbc_desktop.fill_snapshot(snap);

        EXPECT_TRUE(snap.power.on_ac_power);
        EXPECT_FALSE(snap.power.battery_present);
        EXPECT_EQ(snap.power.battery_percent, 255);
    }

    // checks every field for a laptop on battery, mouse battery etc should be skipped
    TEST_F(PowerCollectorTest, DischargingLaptopComputesValues) {
        write_fixture(base_path + "/BAT0/type", type_battery);
        write_fixture(base_path + "/BAT0/uevent", bat0_discharging);
        write_fixture(base_path + "/AC/type", type_mains);
        write_fixture(base_path + "/AC/uevent", ac_offline);
        write_fixture(base_path + "/hidpp_battery_0/type", type_battery);
        write_fixture(base_path + "/hidpp_battery_0/uevent", mouse);

        PowerCollector collector(base_path, 1);
        MetricSnapshot snap;

        ASSERT_TRUE(collector.initialize());
        collector.fill_snapshot(snap);

        EXPECT_TRUE(snap.power.battery_present);
        EXPECT_FALSE(snap.power.on_ac_power);
        EXPECT_EQ(snap.power.battery_percent, 80);
        EXPECT_EQ(snap.power.battery_seconds_remaining, 14400);
        EXPECT_EQ(snap.power.battery_full_seconds_remaining, 18000);
        EXPECT_NEAR(snap.power.battery_power_watts, 10.0f, 0.01f);
        EXPECT_NEAR(snap.power.battery_health_percent, 80.0f, 0.01f);
        EXPECT_EQ(snap.power.battery_cycle_count, 123u);
    }

    // charging laptop is on ac, no time remaining, watts still reported
    TEST_F(PowerCollectorTest, ChargingLaptopOnAc) {
        write_fixture(base_path + "/BAT0/type", type_battery);
        write_fixture(base_path + "/BAT0/uevent", bat0_charging);
        write_fixture(base_path + "/AC/type", type_mains);
        write_fixture(base_path + "/AC/uevent", ac_online);

        PowerCollector collector(base_path, 1);
        MetricSnapshot snap;

        ASSERT_TRUE(collector.initialize());
        collector.fill_snapshot(snap);

        EXPECT_TRUE(snap.power.on_ac_power);
        EXPECT_EQ(snap.power.battery_seconds_remaining, -1);
        EXPECT_EQ(snap.power.battery_full_seconds_remaining, -1);
        EXPECT_NEAR(snap.power.battery_power_watts, 15.0f, 0.01f);
        EXPECT_EQ(snap.power.battery_percent, 80);
    }

    // two charge style batteries with no ac entry, values should be converted and summed
    TEST_F(PowerCollectorTest, TwoChargeStyleBatteriesAreSummed) {
        write_fixture(base_path + "/BAT0/type", type_battery);
        write_fixture(base_path + "/BAT0/uevent", bat0_charge_style);
        write_fixture(base_path + "/BAT1/type", type_battery);
        write_fixture(base_path + "/BAT1/uevent", bat1_charge_style);

        PowerCollector collector(base_path, 1);
        MetricSnapshot snap;

        ASSERT_TRUE(collector.initialize());
        collector.fill_snapshot(snap);

        EXPECT_TRUE(snap.power.battery_present);
        EXPECT_FALSE(snap.power.on_ac_power);
        EXPECT_EQ(snap.power.battery_percent, 65);
        EXPECT_EQ(snap.power.battery_seconds_remaining, 9360);
        EXPECT_EQ(snap.power.battery_full_seconds_remaining, 14400);
        EXPECT_NEAR(snap.power.battery_power_watts, 24.0f, 0.01f);
        EXPECT_NEAR(snap.power.battery_health_percent, 80.0f, 0.01f);
        EXPECT_EQ(snap.power.battery_cycle_count, 250u);
    }

    // with interval 3 it should only pick up a change on the third collect
    TEST_F(PowerCollectorTest, OnlyRereadsEveryInterval) {
        write_fixture(base_path + "/BAT0/type", type_battery);
        write_fixture(base_path + "/BAT0/uevent", bat0_discharging);
        write_fixture(base_path + "/AC/type", type_mains);
        write_fixture(base_path + "/AC/uevent", ac_offline);

        PowerCollector collector(base_path, 3);
        MetricSnapshot snap;

        ASSERT_TRUE(collector.initialize());
        write_fixture(base_path + "/AC/uevent", ac_online);

        collector.collect();
        collector.collect();
        collector.fill_snapshot(snap);
        EXPECT_FALSE(snap.power.on_ac_power);

        collector.collect();
        collector.fill_snapshot(snap);
        EXPECT_TRUE(snap.power.on_ac_power);
    }

} // namespace pulsedb
