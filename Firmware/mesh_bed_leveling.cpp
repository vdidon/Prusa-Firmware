#include "mesh_bed_leveling.h"
#include "mesh_bed_calibration.h"
#include "Configuration.h"

#ifdef MESH_BED_LEVELING

mesh_bed_leveling mbl;

void mesh_bed_leveling::reset() {
    active = 0;
    memset(z_values, 0, sizeof(z_values));
}

float mesh_bed_leveling::get_z(float x, float y) {
    // Position in mesh cells. Called for every planned segment: the constant divisions are multiplications by
    // the inverse, and the truncation is floor() for the positive positions, the negative ones land on the
    // first cell anyway. Out of the mesh, s and t extrapolate the border cell.
    float s = (x - (BED_X0 + X_PROBE_OFFSET_FROM_EXTRUDER)) * (1.f / x_mesh_density);
    int8_t i = (s < MESH_NUM_X_POINTS - 2) ? int8_t(s) : MESH_NUM_X_POINTS - 2;
    if (i < 0)
        i = 0;
    s -= i;

    float t = (y - (BED_Y0 + Y_PROBE_OFFSET_FROM_EXTRUDER)) * (1.f / y_mesh_density);
    int8_t j = (t < MESH_NUM_Y_POINTS - 2) ? int8_t(t) : MESH_NUM_Y_POINTS - 2;
    if (j < 0)
        j = 0;
    t -= j;

    float z0 = z_values[j  ][i] + s * (z_values[j  ][i+1] - z_values[j  ][i]);
    float z1 = z_values[j+1][i] + s * (z_values[j+1][i+1] - z_values[j+1][i]);
    return z0 + t * (z1 - z0);
}
// Works for an odd number of MESH_NUM_X_POINTS and MESH_NUM_Y_POINTS

void mesh_bed_leveling::upsample_3x3()
{
    int idx0 = 0;
    int idx1 = MESH_NUM_X_POINTS / 2;
    int idx2 = MESH_NUM_X_POINTS - 1;
    {
        // First interpolate the points in X axis.
        static const float x0 = (BED_X0 + X_PROBE_OFFSET_FROM_EXTRUDER);
        static const float x1 = 0.5f * float(BED_X0 + BED_Xn) + X_PROBE_OFFSET_FROM_EXTRUDER;
        static const float x2 = BED_Xn + X_PROBE_OFFSET_FROM_EXTRUDER;
        for (int j = 0; j < MESH_NUM_Y_POINTS; ++ j) {
            // Interpolate the remaining values by Largrangian polynomials.
            for (int i = 0; i < MESH_NUM_X_POINTS; ++ i) {
                if (!isnan(z_values[j][i]))
                    continue;
                float x = get_x(i);
                z_values[j][i] =
                    z_values[j][idx0] * (x - x1) * (x - x2) / ((x0 - x1) * (x0 - x2)) +
                    z_values[j][idx1] * (x - x0) * (x - x2) / ((x1 - x0) * (x1 - x2)) +
                    z_values[j][idx2] * (x - x0) * (x - x1) / ((x2 - x0) * (x2 - x1));
            }
        }
    }
    {
        // Second interpolate the points in Y axis.
        static const float y0 = (BED_Y0 + Y_PROBE_OFFSET_FROM_EXTRUDER);
        static const float y1 = 0.5f * float(BED_Y0 + BED_Yn) + Y_PROBE_OFFSET_FROM_EXTRUDER;
        static const float y2 = BED_Yn + Y_PROBE_OFFSET_FROM_EXTRUDER;
        for (int i = 0; i < MESH_NUM_X_POINTS; ++ i) {
            // Interpolate the remaining values by Largrangian polynomials.
            for (int j = 1; j + 1 < MESH_NUM_Y_POINTS; ++ j) {
                if (!isnan(z_values[j][i]))
                    continue;
                float y = get_y(j);
                z_values[j][i] =
                    z_values[idx0][i] * (y - y1) * (y - y2) / ((y0 - y1) * (y0 - y2)) +
                    z_values[idx1][i] * (y - y0) * (y - y2) / ((y1 - y0) * (y1 - y2)) +
                    z_values[idx2][i] * (y - y0) * (y - y1) / ((y2 - y0) * (y2 - y1));
            }
        }
    }
}

void mesh_bed_leveling::print() {
    SERIAL_PROTOCOLLNPGM("Num X,Y: " STRINGIFY(MESH_NUM_X_POINTS) "," STRINGIFY(MESH_NUM_Y_POINTS));
    SERIAL_PROTOCOLPGM("Z search height: "); // STRINGIFY() printed the "f" suffix: "5.0f"
    SERIAL_PROTOCOLLN(MESH_HOME_Z_SEARCH);
    SERIAL_PROTOCOLLNPGM("Measured points:");
    for (uint8_t y = MESH_NUM_Y_POINTS; y-- > 0;) {
        for (uint8_t x = 0; x < MESH_NUM_X_POINTS; x++) {
            SERIAL_PROTOCOLPGM("  ");
            SERIAL_PROTOCOL_F(z_values[y][x], 5);
        }
        SERIAL_PROTOCOLLN();
    }
}

#endif  // MESH_BED_LEVELING
