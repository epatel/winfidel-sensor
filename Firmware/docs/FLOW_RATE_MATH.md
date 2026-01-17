# Flow Rate Compensation Mathematics

This document explains the mathematics behind the automatic flow rate compensation feature in WInFiDEL.

## Problem Statement

Filament diameter variations cause extrusion problems:
- **Thicker filament** = more material per mm of filament fed = over-extrusion
- **Thinner filament** = less material per mm of filament fed = under-extrusion

By measuring the actual filament diameter in real-time, we can compensate by adjusting the printer's flow rate (M221 command).

## Cross-Sectional Area

Filament is (approximately) cylindrical. The cross-sectional area determines how much material passes through the extruder per unit length:

```
Area = π × (diameter/2)² = π × diameter² / 4
```

For nominal 1.75mm filament:
```
Area = π × 1.75² / 4 = 2.405 mm²
```

## Volume Flow Relationship

The extruder pushes a certain length of filament per second. The volumetric flow rate is:

```
Volumetric Flow = Area × Filament Speed
```

If the filament is thicker than expected, the actual volumetric flow is higher than the slicer calculated. To maintain correct extrusion, we must reduce the flow rate proportionally.

## Compensation Formula Derivation

We want the actual volumetric output to match the expected volumetric output:

```
Actual Volume = Expected Volume
```

Let:
- `d_ref` = reference/expected diameter (e.g., 1.75mm)
- `d_meas` = measured diameter
- `flow_ref` = base flow rate (e.g., 100%)
- `flow_new` = compensated flow rate

The areas are proportional to diameter squared:
```
Area_ref / Area_meas = d_ref² / d_meas²
```

To compensate:
```
flow_new = flow_ref × (Area_ref / Area_meas)
flow_new = flow_ref × (d_ref / d_meas)²
```

**Key insight**: The compensation ratio is the *square* of the diameter ratio because area scales with diameter squared.

## Worked Examples

Using a base flow rate of 97% and reference diameter of 1.75mm:

### Example 1: Thick Filament (1.80mm)

```
flow_new = 97 × (1.75 / 1.80)²
flow_new = 97 × (0.9722)²
flow_new = 97 × 0.9452
flow_new = 91.7%
```

The filament is 2.9% thicker in diameter, but the area is 5.7% larger, so we reduce flow by 5.5%.

### Example 2: Thin Filament (1.70mm)

```
flow_new = 97 × (1.75 / 1.70)²
flow_new = 97 × (1.0294)²
flow_new = 97 × 1.0597
flow_new = 102.8%
```

The filament is 2.9% thinner in diameter, but the area is 5.6% smaller, so we increase flow by 6.0%.

### Example 3: Nominal Filament (1.75mm)

```
flow_new = 97 × (1.75 / 1.75)²
flow_new = 97 × 1.0
flow_new = 97%
```

No compensation needed when filament matches reference.

## Why the Square Matters

A common mistake is to use linear compensation:
```
WRONG: flow_new = flow_ref × (d_ref / d_meas)
```

This underestimates the required correction. For 1.80mm filament:
- Linear: 97 × (1.75/1.80) = 94.3% (wrong)
- Quadratic: 97 × (1.75/1.80)² = 91.7% (correct)

The difference (2.6 percentage points) is significant for print quality.

## Safety Limits

The firmware clamps flow rates to configurable min/max values (default 85-115%) to prevent:
- **Extreme under-extrusion** that could cause layer adhesion failure
- **Extreme over-extrusion** that could cause nozzle jams or stringing

If your filament measures outside the range that these limits accommodate, you should not print with that filament as it indicates a quality problem.

## Implementation Notes

1. **Integer rounding**: The M221 command accepts integer percentages. We round to the nearest integer and skip updates if the rounded value hasn't changed.

2. **Rate limiting**: Updates are sent at most once per second (configurable) to avoid flooding the printer with commands.

3. **Threshold filtering**: Small diameter changes below the threshold (default 0.01mm) are ignored to reduce noise.

4. **Base flow rate**: If you normally print at a non-100% flow rate (e.g., 97% for dimensional accuracy), set this as your reference_flow. The compensation will scale from this baseline.
