#define CATCH_CONFIG_MAIN
#include <catch.hpp>
#include <cdf-io.hpp>
#include <cdf.hpp>
#include <iostream>


SCENARIO("Loading a cdf file", "[CDF]")
{
    GIVEN("a cdf file")
    {
        WHEN("file doesn't exist") { REQUIRE(cdf::io::load("wrongfile.cdf") == std::nullopt); }
        WHEN("file exists but isn't a cdf file")
        {
            auto path = std::string(DATA_PATH) + "/not_a_cdf.cdf";
            REQUIRE(std::filesystem::exists(path));
            REQUIRE(cdf::io::load(path) == std::nullopt);
        }
        WHEN("file exists and is a cdf file")
        {
            auto path = std::string(DATA_PATH) + "/a_cdf.cdf";
            REQUIRE(std::filesystem::exists(path));
            auto cd_opt = cdf::io::load(path);
            REQUIRE(cd_opt != std::nullopt);
            auto cd = *cd_opt;
            REQUIRE(cd.attributes.find("attr")!=cd.attributes.cend());
            REQUIRE(cd.attributes.find("attr_float")!=cd.attributes.cend());
            REQUIRE(cd.attributes.find("attr_int")!=cd.attributes.cend());
        }
    }
}
