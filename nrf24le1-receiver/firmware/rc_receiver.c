#include <stdint.h>
#include <stdbool.h>

#include <platform.h>
#include <rc_receiver.h>
#include <persistent_storage.h>
#include <rf.h>
#include <uart0.h>

#define PROTOCOL_3CH 0xaa
#define PROTOCOL_4CH 0xab
#define STICKDATA_PACKETID_3CH 0x55
#define FAILSAFE_PACKETID_3CH 0xaa
#define STICKDATA_PACKETID_4CH 0x56
#define FAILSAFE_PACKETID_4CH 0xab

#define PAYLOAD_SIZE 10
#define ADDRESS_WIDTH 5
#define NUMBER_OF_HOP_CHANNELS 20
#define MAX_HOP_WITHOUT_PACKET 15
#define FIRST_HOP_TIME_IN_US 2500
#define HOP_TIME_IN_US 5000

#define FAILSAFE_TIMEOUT (640 / __SYSTICK_IN_MS)
#define BIND_TIMEOUT (5000 / __SYSTICK_IN_MS)
#define ISP_TIMEOUT (3000 / __SYSTICK_IN_MS)
#define BLINK_TIME_FAILSAFE (320 / __SYSTICK_IN_MS)
#define BLINK_TIME_BINDING (50 / __SYSTICK_IN_MS)

#define LED_STATE_IDLE 0
#define LED_STATE_RECEIVING 1
#define LED_STATE_FAILSAFE 2
#define LED_STATE_BINDING 3

#define BUTTON_PRESSED 0
#define BUTTON_RELEASED 1

// On all hardware variants the LEDs connect to ground, so the IO pin has to
// go high to light them up.
#define LED_ON 1
#define LED_OFF (~LED_ON)

extern bool systick;


__xdata uint16_t channels[NUMBER_OF_CHANNELS];
bool successful_stick_data = false;

#ifdef EXTENDED_PREPROCESSOR_OUTPUT
__xdata uint16_t raw_data[2];
#endif

static bool use_buffer_0;
static __xdata uint16_t pulse_buffer_0_0;
static __xdata uint16_t pulse_buffer_0_1;
static __xdata uint16_t pulse_buffer_0_2;
static __xdata uint16_t pulse_buffer_0_3;
static __xdata uint16_t pulse_buffer_1_0;
static __xdata uint16_t pulse_buffer_1_1;
static __xdata uint16_t pulse_buffer_1_2;
static __xdata uint16_t pulse_buffer_1_3;

static bool rf_int_fired = false;
static uint8_t led_state;
static uint16_t blink_timer;

static __xdata uint8_t payload[PAYLOAD_SIZE];

static uint8_t failsafe_enabled;
static __xdata uint16_t failsafe[NUMBER_OF_CHANNELS];
static uint16_t failsafe_timer;

static __xdata uint8_t model_address[ADDRESS_WIDTH];
static bool perform_hop_requested = false;
static uint8_t hops_without_packet;
static uint8_t hop_index;
static __xdata uint8_t hop_data[NUMBER_OF_HOP_CHANNELS];

static bool binding_requested = false;
static bool binding = false;
static uint16_t bind_timer;
static const uint8_t BIND_CHANNEL = 0x51;
static const uint8_t BIND_ADDRESS[ADDRESS_WIDTH] = {0x12, 0x23, 0x23, 0x45, 0x78};
static __xdata uint8_t bind_storage_area[NUMBER_OF_PERSISTENT_ELEMENTS];
#define PROTOCOLID_INDEX (sizeof(bind_storage_area)-1)

static uint8_t stickdata_packetid;
static uint8_t failsafe_packetid;


// ****************************************************************************
// static void print_payload(void)
// {
//     uint8_t i;
//     for (i = 0; i < PAYLOAD_SIZE; i++) {
//         uart0_send_uint8_hex(payload[i]);
//         uart0_send_char(' ');
//     }
//     uart0_send_linefeed();
// }


// ****************************************************************************
static void initialize_failsafe(void) {
    uint8_t i;

    failsafe_enabled = false;
    failsafe_timer = FAILSAFE_TIMEOUT;
    for (i = 0; i < NUMBER_OF_CHANNELS; i++) {
        failsafe[i] = SERVO_PULSE_CENTER;
    }
}


// ****************************************************************************
static void output_pulses(void)
{
    if (use_buffer_0) {
        pulse_buffer_1_0 = channels[0];
        pulse_buffer_1_1 = channels[1];
        pulse_buffer_1_2 = channels[2];
        pulse_buffer_1_3 = channels[3];
        use_buffer_0 = false;
    }
    else {
        pulse_buffer_0_0 = channels[0];
        pulse_buffer_0_1 = channels[1];
        pulse_buffer_0_2 = channels[2];
        pulse_buffer_0_3 = channels[3];
        use_buffer_0 = true;
    }
}


// ****************************************************************************
// Not needed for the nRF24LE1 port as the data sent by the transmitter
// alreaday corresponds to the timer value for the servo pulse
static uint16_t stickdata2ms(uint16_t stickdata)
{
    // uint16_t ms;

    // // ms = (0xffff - stickdata) * 3 / 4;
    // ms = (0xffff - stickdata);
    // return ms & 0xffff;

    return stickdata;
}


// ****************************************************************************
// This code undoes the value scaling that the transmitter nRF module does
// when receiving a 12 bit channel value via the UART, while forming the
// transmit packet.
//
// The transmitter calculates
//
//    value_sent = (uart_data * 14 / 10) + 0xf200
//
// As such the input range via the UART can be 0x000 .. 0x9ff
// ****************************************************************************
static uint16_t stickdata2txdata(uint16_t stickdata)
{
    return (stickdata - 0xf200) * 10 / 14;
}


// ****************************************************************************
static void stop_hop_timer(void)
{
    T2CON = 0;              // Stop timer 2
    perform_hop_requested = false;
}


// ****************************************************************************
static void restart_hop_timer(void)
{
    T2CON = 0;              // Stop timer 2
    TIMER2 = TIMER_VALUE_US(FIRST_HOP_TIME_IN_US);
    T2CON = 0x01;           // Timer 2 clock = f/12, Reload Mode 0

    hops_without_packet = 0;
    perform_hop_requested = false;
}


// ****************************************************************************
static void restart_packet_receiving(void)
{
    stop_hop_timer();

    rf_clear_ce();
    hop_index = 0;
    hops_without_packet = 0;
    perform_hop_requested = false;
    rf_set_rx_address(DATA_PIPE_0, ADDRESS_WIDTH, model_address);
    rf_set_channel(hop_data[0]);
    rf_flush_rx_fifo();
    rf_clear_irq(RX_RD);
    rf_int_fired = false;
    rf_set_ce();
}


// ****************************************************************************
static void parse_bind_data(void)
{
    uint8_t i;

    for (i = 0; i < ADDRESS_WIDTH; i++) {
        model_address[i] = bind_storage_area[i];
    }

    for (i = 0; i < NUMBER_OF_HOP_CHANNELS; i++) {
        hop_data[i] = bind_storage_area[ADDRESS_WIDTH + i];
    }

    if (bind_storage_area[PROTOCOLID_INDEX] != PROTOCOL_4CH) {
        init_uart0();
        stickdata_packetid = STICKDATA_PACKETID_3CH;
        failsafe_packetid = FAILSAFE_PACKETID_3CH;
    }
    else {
        disable_uart0();
        stickdata_packetid = STICKDATA_PACKETID_4CH;
        failsafe_packetid = FAILSAFE_PACKETID_4CH;
    }
}


// ****************************************************************************
static void binding_done(void)
{
    led_state = LED_STATE_IDLE;
    failsafe_timer = FAILSAFE_TIMEOUT;
    binding = false;
    binding_requested = false;

    restart_packet_receiving();
}


// ****************************************************************************
// The bind process works as follows:
//
// The transmitter regularly sends data on the fixed channel 51h, with address
// 12:23:23:45:78.
// This data is sent at a lower power. All transmitters do that all the time.
//
// A bind data packet is sent every 5ms.
//
// The transmitter cycles through 4 packets:
// ff aa 55 a1 a2 a3 a4 a5 .. ..
// cc cc 00 ha hb hc hd he hf hg
// cc cc 01 hh hi hj hk hl hm hn
// cc cc 02 ho hp hq hr hs ht ..
//
// ff aa 55     Special marker for the first packet
// a[1-5]       The 5 address bytes
// cc cc        A 16 byte checksum of bytes a1..a5
// h[a-t]       20 channels for frequency hopping
// ..           Not used
//
// The receiver also supports a modded protocol (LANEBoysRC-4ch) with 4 channels
// channels, During binding the special marker for the first packet is
// "ff ab 56" for this protocol.
//
// ****************************************************************************
static void process_binding(void)
{
    static uint8_t bind_state;
    static uint16_t checksum;
    uint8_t i;

    // ================================
    if (!binding) {
        if (!binding_requested) {
            return;
        }

        led_state = LED_STATE_BINDING;
        binding = true;
        bind_state = 0;
        bind_timer = BIND_TIMEOUT;

#ifndef NO_DEBUG
        uart0_send_cstring("Starting bind procedure\n");
#endif

        rf_clear_ce();
        // Set special address 12h 23h 23h 45h 78h
        rf_set_rx_address(0, ADDRESS_WIDTH, BIND_ADDRESS);
        // Set special channel 0x51
        rf_set_channel(BIND_CHANNEL);
        rf_set_ce();
        return;
    }


    // ================================
    if (bind_timer == 0) {
#ifndef NO_DEBUG
        uart0_send_cstring("Bind timeout\n");
#endif
        binding_done();
        return;
    }


    // ================================
    if (!rf_int_fired) {
        return;
    }
    rf_int_fired = false;

    while (!rf_is_rx_fifo_emtpy()) {
        rf_read_fifo(payload, PAYLOAD_SIZE);
    }
    rf_clear_irq(RX_RD);

    switch (bind_state) {
        case 0:
            if (payload[0] == 0xff) {
                if (((payload[1] == 0xaa) && (payload[2] == 0x55)) ||
                    ((payload[1] == 0xab) && (payload[2] == 0x56))) {

                    // Save the protocol identifier (PROTOCOL_3CH=0xaa or PROTOCOL_4CH=0xab)
                    bind_storage_area[PROTOCOLID_INDEX] = payload[1];

                    checksum = 0;
                    for (i = 0; i < 5; i++) {
                        uint8_t payload_byte;

                        payload_byte = payload[3 + i];
                        bind_storage_area[i] = payload_byte;
                        checksum += payload_byte;
                    }
                    bind_state = 1;
                }
            }
            break;

        case 1:
            if (payload[0] == (checksum & 0xff)) {
                if (payload[1] == (checksum >> 8)) {
                    if (payload[2] == 0) {
                        for (i = 0; i < 7; i++) {
                            bind_storage_area[5 + i] = payload[3 + i];
                        }
                        bind_state = 2;
                    }
                }
            }
            break;

        case 2:
            if (payload[0] == (checksum & 0xff)) {
                if (payload[1] == (checksum >> 8)) {
                    if (payload[2] == 1) {
                        for (i = 0; i < 7; i++) {
                            bind_storage_area[12 + i] = payload[3 + i];
                        }
                        bind_state = 3;
                    }
                }
            }
            break;

        case 3:
            if (payload[0] == (checksum & 0xff)) {
                if (payload[1] == (checksum >> 8)) {
                    if (payload[2] == 2) {
                        for (i = 0; i < 6; i++) {
                            bind_storage_area[19 + i] = payload[3 + i];
                        }

                        save_persistent_storage(bind_storage_area);
                        parse_bind_data();
#ifndef NO_DEBUG
                        uart0_send_cstring("Bind successful\n");
#endif
                        binding_done();
                        return;
                    }
                }
            }
            break;

        default:
            bind_state = 0;
            break;
    }
}


// ****************************************************************************
static void process_receiving(void)
{
    // ================================
    if (binding) {
        return;
    }

    // ================================
    // Process failsafe only if we ever got a successsful stick data payload
    // after reset.
    //
    // This way the servo outputs stay off until we got successful stick
    // data, so the servos do not got to the failsafe point after power up
    // in case the transmitter is not on yet.
    if (successful_stick_data) {
        if (failsafe_timer == 0) {
            uint8_t i;

            for (i = 0; i < NUMBER_OF_CHANNELS; i++) {
                channels[i] = failsafe[i];
            }
            output_pulses();

            led_state = LED_STATE_FAILSAFE;
        }
    }


    // ================================
    if (perform_hop_requested) {
        perform_hop_requested = false;
        ++hops_without_packet;

        // If we are missing too many packets we resync by switching to the
        // first channel and waiting for a packet without hopping.
        if (hops_without_packet > MAX_HOP_WITHOUT_PACKET) {
            restart_packet_receiving();
        }
        else {
            rf_clear_ce();
            hop_index = (hop_index + 1) % NUMBER_OF_HOP_CHANNELS;
            rf_set_channel(hop_data[hop_index]);
            rf_set_ce();
        }
    }


    // ================================
    if (!rf_int_fired) {
        return;
    }
    rf_int_fired = false;

    while (!rf_is_rx_fifo_emtpy()) {
        rf_read_fifo(payload, PAYLOAD_SIZE);
    }
    rf_clear_irq(RX_RD);

    restart_hop_timer();


    // ================================
    // payload[7] is 0x55 for stick data
    if (payload[7] == stickdata_packetid) {
        channels[0] = (payload[1] << 8) + payload[0];
        channels[1] = (payload[3] << 8) + payload[2];
        channels[2] = (payload[5] << 8) + payload[4];
        channels[3] = (payload[9] << 8) + payload[6];
        output_pulses();

#ifdef EXTENDED_PREPROCESSOR_OUTPUT
        // Save raw received data for the pre-processor to output, so someone
        // can build custom extension based on hijacking channel 3 and using
        // the unused payload bytes 6 and 9.
        // Note:
        //   - See hk310-expansion project for hijacking channel 3
        //   - Custom nRF module firmware required in the transmitter to utilize
        //     payload 6 + 9
        raw_data[0] = stickdata2txdata((payload[5] << 8) + payload[4]);
        raw_data[1] = (payload[6] << 8) + payload[9];
#endif

        successful_stick_data = true;

        failsafe_timer = FAILSAFE_TIMEOUT;
        led_state = LED_STATE_RECEIVING;
    }
    // ================================
    // payload[7] is 0xaa for failsafe data
    else if (payload[7] == failsafe_packetid) {
        // payload[8]: 0x5a if enabled, 0x5b if disabled
        if (payload[8] == 0x5a) {
            failsafe_enabled = true;
            failsafe[0] = (payload[1] << 8) + payload[0];
            failsafe[1] = (payload[3] << 8) + payload[2];
            failsafe[2] = (payload[5] << 8) + payload[4];
            failsafe[3] = (payload[9] << 8) + payload[6];
        }
        else {
            // If failsafe is disabled use default values of 1500ms, just
            // like the HKR3000 and XR3100 do.
            initialize_failsafe();
        }
    }
}


// ****************************************************************************
static void process_systick(void)
{
    if (!systick) {
        return;
    }

    if (failsafe_timer) {
        --failsafe_timer;
    }

    if (bind_timer) {
        --bind_timer;
    }

    if (blink_timer) {
        --blink_timer;
    }
}


// ****************************************************************************
static void process_bind_button(void)
{
    static bool old_button_state = BUTTON_RELEASED;
    bool new_button_state;

    if (!systick) {
        return;
    }

    new_button_state = GPIO_BIND;

    if (new_button_state == old_button_state) {
        return;
    }
    old_button_state = new_button_state;

    if (new_button_state == BUTTON_PRESSED) {
        binding_requested = true;
    }
}


// ****************************************************************************
static void process_led(void)
{
    static uint8_t old_led_state = 0xff;
    static bool blinking;
    static uint16_t blink_timer_reload_value;


    if (blinking) {
        if (blink_timer == 0) {
            blink_timer = blink_timer_reload_value;
            GPIO_LED = !GPIO_LED;
        }
    }

    if (led_state == old_led_state) {
        return;
    }
    old_led_state = led_state;

    switch (led_state) {
        case LED_STATE_RECEIVING:
#ifdef GPIO_LED_GREEN
            GPIO_LED_GREEN = LED_ON;
            GPIO_LED = LED_OFF;
#else
            GPIO_LED = LED_ON;
#endif
            blinking = false;
            break;

        case LED_STATE_BINDING:
#ifdef GPIO_LED_GREEN
            GPIO_LED_GREEN = LED_OFF;
            GPIO_LED = LED_ON;
#else
            // Single LED: Start blinking with a dark phase
            GPIO_LED = LED_OFF;
#endif
            blink_timer_reload_value = BLINK_TIME_BINDING;
            blinking = true;
            break;

        case LED_STATE_IDLE:
        case LED_STATE_FAILSAFE:
        default:
#ifdef GPIO_LED_GREEN
            GPIO_LED_GREEN = LED_OFF;
            GPIO_LED = LED_ON;
#else
            // Single LED: Start blinking with a dark phase
            GPIO_LED = LED_OFF;
#endif
            blink_timer_reload_value = BLINK_TIME_FAILSAFE;
            blinking = true;
            break;
    }
}


// ****************************************************************************
void init_receiver(void)
{
    load_persistent_storage(bind_storage_area);
    parse_bind_data();
    initialize_failsafe();

    rf_enable_clock();
    rf_clear_ce();
    rf_enable_receiver();

    rf_set_crc(CRC_2_BYTES);
    rf_set_irq_source(RX_RD);
    rf_set_data_rate(DATA_RATE_250K);
    rf_set_data_pipes(DATA_PIPE_0, NO_AUTO_ACKNOWLEDGE);
    rf_set_address_width(ADDRESS_WIDTH);
    rf_set_payload_size(DATA_PIPE_0, PAYLOAD_SIZE);

    restart_packet_receiving();

    led_state = LED_STATE_IDLE;
}


// ****************************************************************************
void process_receiver(void)
{
    process_systick();
    process_bind_button();
    process_binding();
    process_receiving();
    process_led();
}


// ****************************************************************************
// RF_IRQ interrupt handler
// ****************************************************************************
void rf_interrupt_handler(void) __interrupt ((0x004b - 3) / 8)
{
    rf_int_fired = true;
}


// ****************************************************************************
// Timer 2 interrupt handler
// ****************************************************************************
void hop_timer_handler(void) __interrupt ((0x002b - 3) / 8)
{
    IRCON_tf2 = 0;          // Clear the interrupt flag
    TIMER2 = TIMER_VALUE_US(HOP_TIME_IN_US);

    perform_hop_requested = true;
}


// ****************************************************************************
// Timer 1 interrupt handler
// ****************************************************************************
void servo_pulse_timer_handler(void) __interrupt ((0x001b - 3) / 8) __using (1)
{
    static uint8_t servo_pulse_state;

    // Stop Timer1
    // This is very important as otherwise we create potential jitter,
    // presumably because the timer may decrement between writing the low and
    // high byte, and the low byte may roll-over, decrementing the high byte
    // before applying the correct low value.
    //
    // Stopping the timer and then restarting resolves the issue, as it is
    // not possible for the timer to decrement while we are writing the
    // new value.
    TCON_tr1 = 0;

    // The HKR3000 and HKR3100 hardware share the PPM and CH4 pin. We therefore
    // output PPM only in 3-channel mode (when stickdata_packetid is not
    // the 4-channel packet id 0x56)
#if HARDWARE == NRF24LE1_MODULE
    GPIO_PPM = 0;
#else
    if (stickdata_packetid != STICKDATA_PACKETID_4CH) {
        GPIO_PPM = 0;
    }
#endif

    // This code has been written to provide reasonably effectient assembler
    // code for SDCC.
    //
    // - if/else if/else is used instead of switch, as switch does indirect
    //   jumps and uses multiplication to calculate the jump address
    //
    // - Instead of using an array for the double-buffering of servo pulses
    //   individual variables are used to prevent expensive 16-bit pointer
    //   arithmetic.
    //
    // - The register pair TH1/TL1 is declared as __sfr16 TIMER1 for compact
    //   code

    ++servo_pulse_state;
    if (servo_pulse_state == 1) {
        GPIO_CH1 = 1;
        TIMER1 = use_buffer_0 ? pulse_buffer_0_0 : pulse_buffer_1_0;
        TCON_tr1 = 1;
    }
    else if (servo_pulse_state == 2) {
        GPIO_CH1 = 0;
        GPIO_CH2 = 1;
        TIMER1 = use_buffer_0 ? pulse_buffer_0_1 : pulse_buffer_1_1;
        TCON_tr1 = 1;
    }
    else if (servo_pulse_state == 3) {
        GPIO_CH2 = 0;
        GPIO_CH3 = 1;
        TIMER1 = use_buffer_0 ? pulse_buffer_0_2 : pulse_buffer_1_2;
        TCON_tr1 = 1;
    }
    else if (servo_pulse_state == 4) {
        if (stickdata_packetid == STICKDATA_PACKETID_4CH) {
            // 4-channel protocol active
            GPIO_CH3 = 0;
            GPIO_CH4 = 1;
            TIMER1 = use_buffer_0 ? pulse_buffer_0_3 : pulse_buffer_1_3;
            TCON_tr1 = 1;
        }
        else {
            // 3-channel protocol active
            GPIO_CH3 = 0;
            // All done: reset for the next pulse train
            servo_pulse_state = 0;
        }
    }
    else {
        GPIO_CH4 = 0;
        // All done: stop the timer and reset for the next pulse train
        TCON_tr1 = 0;
        servo_pulse_state = 0;
    }

#if HARDWARE == NRF24LE1_MODULE
    GPIO_PPM = 1;
#else
    if (stickdata_packetid != 0x56) {
        GPIO_PPM = 1;
    }
#endif
}


