#include <stdint.h>

#include <curves.h>
#include <inputs.h>
#include <mixer.h>


int32_t channels[NUMBER_OF_CHANNELS];

static mixer_unit_t mixer_units[NUMBER_OF_MIXER_UNITS] = {
    {
        .src = 1,
        .invert_source = 0,
        .dest = CH1,
        .curve = {
            .type = CURVE_EXPO,
            .points = {50, 50}
        },
        .scalar = 100,
        .offset = 1
    },
    {
        .src = 2,
        .dest = CH2,
        .curve = {
            .type = CURVE_NONE,
        },
        .scalar = 100
    },
    {
        .src = 3,
        .dest = CH3,
        .curve = {
            .type = CURVE_NONE,
        },
        .scalar = 100
    },
    {
        .src = 4,
        .dest = CH4,
        .curve = {
            .type = CURVE_NONE,
        },
        .scalar = 100
    },
    {
        .src = 0
    }
};


// ****************************************************************************
static void apply_mixer_unit(mixer_unit_t *m)
{
    int32_t value;

    // 1st: Get source value with trim
    value = INPUTS_get_input(m->src);

    // Invert if necessary
    if (m->invert_source) {
        value = - value;
    }

    // 2nd: apply curve
    value = CURVE_evaluate(&m->curve, value);

    // 3rd: apply scalar and offset
     value = value * m->scalar / 100 + PERCENT_TO_CHANNEL(m->offset);

     channels[m->dest] = value;
}


// ****************************************************************************
void MIXER_evaluate(void)
{
    INPUTS_filter_and_normalize();

    for (ch_t i = FIRST_HARDWARE_CHANNEL; i <= LAST_HARDWARE_CHANNEL; i++) {
        channels[i] = 0;
    }

    for (unsigned i = 0; i < NUMBER_OF_MIXER_UNITS; i++) {
        mixer_unit_t *m = &mixer_units[i];
        if (m->src == 0) {
            break;
        }

        apply_mixer_unit(m);
    }
}


// ****************************************************************************
void MIXER_init(void)
{

}