// V2 objects (build spec §3.1, §3.2): factories, volumes, heat input, validation.

#include "core/fem3d/Design.hpp"

#include <cmath>
#include <limits>

#include <gtest/gtest.h>

using namespace uniforge::fem3d;
using uniforge::ErrorCode;

TEST(Design, EveryLadderObjectIsValid) {
    for (const Design& d :
         {makeV1Bar(), makeCube(), makePlate(), makeFinnedPlate(), makeHS7(), makePinFin()}) {
        EXPECT_TRUE(validate(d).ok()) << d.name;
    }
    EXPECT_EQ(makeV1Bar().ladderLevel, "L0a");
    EXPECT_EQ(makeCube().ladderLevel, "L0b");
    EXPECT_EQ(makePlate().ladderLevel, "L1");
    EXPECT_EQ(makeFinnedPlate().ladderLevel, "L2");
    EXPECT_EQ(makeHS7().ladderLevel, "L3");
    EXPECT_EQ(makePinFin().ladderLevel, "L4");
}

TEST(Design, BarAndCubeHaveTheV1VolumeAndHeat) {
    const Design bar = makeV1Bar();
    const Design cube = makeCube();
    EXPECT_NEAR(bar.volume(), 1e-4, 1e-18);
    EXPECT_NEAR(cube.volume(), 1e-4, 1e-16);
    EXPECT_NEAR(bar.heatGenerated(), 1.0, 1e-12); // q·A·L = 1 W, as in v1
    EXPECT_NEAR(cube.heatGenerated(), 1.0, 1e-10);
    EXPECT_EQ(bar.boxes[0].faces[XMin].kind, BcKind::Dirichlet);
    EXPECT_EQ(bar.boxes[0].faces[XMin].t, 100.0);
    EXPECT_EQ(bar.boxes[0].faces[XMax].t, 20.0);
    EXPECT_EQ(bar.boxes[0].faces[YMin].kind, BcKind::Insulated);
}

TEST(Design, HeatSinkMatchesTheV1SampleAndTheSpec) {
    const Design hs = makeHS7();
    // Base + 7 fins = 2.416e-5 m³, the volume of v1's data/geometry/heatsink_fins.stl.
    double aluminium = 0.0;
    for (const Box& b : hs.boxes) {
        if (b.material == 0) {
            aluminium += b.volume();
        }
    }
    EXPECT_NEAR(aluminium, 2.416e-5, 1e-17);
    EXPECT_EQ(hs.boxes.size(), 1U + 7U + 2U);    // base, fins, TIM, die
    EXPECT_NEAR(hs.heatGenerated(), 30.0, 1e-9); // P = 30 W in the die
    ASSERT_EQ(hs.fixedLayers.size(), 1U);
    EXPECT_EQ(hs.fixedLayers[0].axis, 2);
    EXPECT_NEAR(hs.fixedLayers[0].hi - hs.fixedLayers[0].lo, 1e-4, 1e-15);
    // No TIM, no fixed layer below L3.
    EXPECT_TRUE(makeFinnedPlate().fixedLayers.empty());
    EXPECT_EQ(makePlate().boxes.size(), 2U);
    EXPECT_EQ(makePinFin().boxes.size(), 3U + 48U);
}

TEST(Design, ValidationRejectsBrokenDesigns) {
    // HS-7 box order: base, TIM, die, fin 1 … fin 7.
    Design d = makeHS7();
    ASSERT_EQ(d.boxes[3].name, "fin 1");
    d.boxes[3].min[0] -= 0.001; // fin 1 is 1 mm thicker but still clear of fin 2: valid
    EXPECT_TRUE(validate(d).ok());

    d = makeHS7();
    d.boxes[3].min[2] -= 0.001; // fin 1 reaches 1 mm into the base: overlap
    EXPECT_EQ(validate(d).error().code, ErrorCode::InvalidInput);

    d = makeHS7();
    d.boxes[3].max[0] += 0.009; // fin 1 now runs into fin 2
    EXPECT_EQ(validate(d).error().code, ErrorCode::InvalidInput);

    d = makeV1Bar();
    d.boxes[0].max[1] = d.boxes[0].min[1];
    EXPECT_EQ(validate(d).error().code, ErrorCode::InvalidInput);

    d = makeV1Bar();
    d.boxes[0].material = 3;
    EXPECT_EQ(validate(d).error().code, ErrorCode::InvalidInput);

    d = makeV1Bar();
    d.materials[0].k = 0.0;
    EXPECT_EQ(validate(d).error().code, ErrorCode::InvalidInput);

    d = makeV1Bar();
    d.materials[0].q = std::numeric_limits<double>::quiet_NaN();
    EXPECT_EQ(validate(d).error().code, ErrorCode::NonFinite);

    d = makeHS7();
    d.boxes[0].faces[ZMax].h = -1.0;
    EXPECT_EQ(validate(d).error().code, ErrorCode::InvalidInput);

    d = makeHS7();
    d.fixedLayers[0].hi = d.fixedLayers[0].lo;
    EXPECT_EQ(validate(d).error().code, ErrorCode::InvalidInput);

    d.boxes.clear();
    EXPECT_EQ(validate(d).error().code, ErrorCode::InvalidInput);
}
