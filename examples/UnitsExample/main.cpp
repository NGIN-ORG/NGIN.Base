#include <iostream>
#include <print>

#include <NGIN/Units.hpp>
using namespace NGIN::Units;

int main()
{

    using CustomUnit = Unit<LENGTH, double, RatioPolicy<1, 873758>>;
    // Basic construction and output
    Meters       m(5.0);
    Kilometers   km(1.2);
    Seconds      s(10.0);
    Milliseconds ms(500.0);
    Celsius      c(25.0);
    Kelvin       k  = UnitCast<Kelvin>(c);
    Celsius      c2 = UnitCast<Celsius>(k);
    CustomUnit   customUnit(3.0);

    std::cout << "Meters: " << m << std::endl;
    std::cout << "Kilometers: " << km << std::endl;
    std::cout << "Seconds: " << s << std::endl;
    std::cout << "Milliseconds: " << ms << std::endl;
    std::cout << "Celsius: " << c << std::endl;
    std::cout << "Celsius to Kelvin: " << k << std::endl;
    std::cout << "Kelvin to Celsius: " << c2 << std::endl;

    // Arithmetic
    auto totalM = m + UnitCast<Meters>(Kilometers(1.0));
    std::cout << "Total meters (5m + 1km): " << totalM << std::endl;

    auto speed = m / s;
    std::cout << "Speed (m/s): " << speed.GetValue() << std::endl;

    // Affine conversion test
    Celsius freezing(0.0);
    Kelvin  freezingK = UnitCast<Kelvin>(freezing);
    std::cout << "Freezing point: " << freezing << " = " << freezingK << std::endl;

    // Custom unit output
    std::cout << "Custom Unit (No UnitTraits): " << customUnit << std::endl;

    /// Formatting example
    Seconds t {3.5};
    Meters  d {12.0};
    std::println("Speed: {:.1f}", (d / t));// prints “Speed: 3.4 m/s”

    return 0;
}
