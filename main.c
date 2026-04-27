#ifdef _WIN32
  #include <windows.h>
#endif
#include "config.h"
#define SDL_MAIN_USE_CALLBACKS
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#define COLOR_GRAY   "\033[0;90m"
#define COLOR_RED    "\033[0;91m"
#define COLOR_YELLOW "\033[0;93m"
#define COLOR_BLUE   "\033[0;94m"
#define COLOR_END    "\033[0m"

#define VID_NINTENDO                             0x057e
#define PID_NINTENDO_SWITCH2_GAMECUBE_CONTROLLER 0x2073
#define PID_NINTENDO_SWITCH2_JOYCON_LEFT         0x2067
#define PID_NINTENDO_SWITCH2_JOYCON_PAIR         0x2068
#define PID_NINTENDO_SWITCH2_JOYCON_RIGHT        0x2066
#define PID_NINTENDO_SWITCH2_PRO                 0x2069

#define MIN_RATE               10                             // Hz
#define MAX_RATE               10000                          // Hz
#define MIN_INTERVAL           (SDL_NS_PER_SECOND / MAX_RATE) // nanoseconds
#define MAX_INTERVAL           (SDL_NS_PER_SECOND / MIN_RATE) // nanoseconds
#define NUM_SENSOR_AXES        3                              // Gyro or accel
#define IMU_SAMPLES_PER_PACKET 3                              // Switch only
#define IMU_SAMPLES_DUP_WINDOW (IMU_SAMPLES_PER_PACKET * 2)   // Switch only
#define DUP_SWITCH_THRESHOLD   10.0f                          // percent
#define DUP_WARN_THRESHOLD     0.1f                           // percent
#define DEFAULT_MEASURE_TIME   3                              // seconds
#define MAX_MEASURE_TIME       60                             // seconds
#define MAX_RESULTS_INTERVAL   99.999                         // milliseconds
#define MAX_RESULTS_RATE       999999                         // Hz
#define MAX_RESULTS_COUNT      999999                         // count
#define MS_PER_NS              1.0e-6
#define MS_PER_S               1000.0

typedef struct
{
    double mean;
    double sd;
    double min;
    double p1;
    double p5;
    double p25;
    double p50;
    double p75;
    double p95;
    double p99;
    double max;
} stats_set_t;

typedef struct
{
    stats_set_t interval; // Sample interval (milliseconds).
    stats_set_t rate;     // Sample rate (Hz).
    size_t count;         // Valid sample count.
    size_t num_samples;   // Total Sample count.
    size_t num_duplicate;
    size_t num_invalid;
} stats_t;

typedef struct
{
    uint64_t *timestamp;            // Timestamps (nanoseconds).
    float (*data)[NUM_SENSOR_AXES]; // Gyro or accel data.
    size_t num_samples;             // Samples collected.
    stats_t stats;                  // Descriptive statistics.
    bool enabled;                   // Supported by this controller?
} sensor_t;

typedef struct
{
    int16_t *value;            // Axis values (-32768 to 32767).
    SDL_GamepadAxis axis_type; // SDL axis type.
} axis_t;

typedef struct
{
    uint64_t *timestamp; // Timestamps (nanoseconds).
    axis_t x;            // Stick x-axis.
    axis_t y;            // Stick y-axis.
    size_t num_samples;  // Samples collected.
    stats_t stats;       // Descriptive statistics.
    bool enabled;        // Supported by this controller?
} stick_t;

typedef struct
{
    SDL_Gamepad *device;    // Connected controller.
    SDL_JoystickID id;      // Instance ID for connected controller.
    const char *name;       // Controller name returned by SDL.
    bool switch_controller; // Is this a Switch controller?
} gamepad_t;

typedef enum
{
    STATE_START,
    STATE_CONNECT,
    STATE_WAIT,
    STATE_MEASURE,
    STATE_DONE
} state_t;

typedef struct
{
    state_t state;
    uint64_t interval;        // Iteration interval (nanoseconds).
    uint64_t last_time;       // Last iteration time (nanoseconds).
    uint64_t measure_elapsed; // Measurement elapsed time (seconds).
    uint64_t measure_time;    // How long to collect samples (seconds).
    size_t max_samples;       // Max number of samples to collect.
    double *delta_time;       // Time between samples (nanoseconds).
    gamepad_t gamepad;
    stats_t poll;
    stick_t left;
    stick_t right;
    sensor_t gyro;
    sensor_t accel;
    bool verbose;
    const char *gray;
    const char *red;
    const char *yellow;
    const char *blue;
    const char *end;
#ifdef _WIN32
    HANDLE hconsole;
    DWORD original_mode;
#endif
} appstate_t;

static void InitLog(appstate_t *as)
{
#ifdef _WIN32
    if (as->hconsole == NULL)
    {
        as->gray = as->red = as->yellow = as->blue = as->end = "";
    }
    else
#endif
    {
        as->gray = COLOR_GRAY;
        as->red = COLOR_RED;
        as->yellow = COLOR_YELLOW;
        as->blue = COLOR_BLUE;
        as->end = COLOR_END;
    }
}

#ifdef _WIN32
static void EnableVtProcessing(appstate_t *as)
{
    as->hconsole = GetStdHandle(STD_OUTPUT_HANDLE);
    if (as->hconsole != INVALID_HANDLE_VALUE)
    {
        if (GetConsoleMode(as->hconsole, &as->original_mode))
        {
            const DWORD flags =
                (ENABLE_PROCESSED_OUTPUT | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
            if (SetConsoleMode(as->hconsole, flags))
            {
                return;
            }
        }
    }
    as->hconsole = NULL;
}
#endif

static void ResetAppState(appstate_t *as)
{
#define AS_ZEROP(ptr) SDL_memset((ptr), 0, sizeof(*(ptr)) * as->max_samples)

    as->state = STATE_START;
    as->interval = 0;
    as->last_time = 0;
    as->measure_elapsed = 0;
    AS_ZEROP(as->delta_time);
    SDL_zero(as->gamepad);
    SDL_zero(as->poll);

    stick_t *sticks[] = {&as->left, &as->right};
    for (size_t i = 0; i < SDL_arraysize(sticks); i++)
    {
        AS_ZEROP(sticks[i]->timestamp);
        AS_ZEROP(sticks[i]->x.value);
        AS_ZEROP(sticks[i]->y.value);
        sticks[i]->num_samples = 0;
        SDL_zero(sticks[i]->stats);
        sticks[i]->enabled = false;
    }

    sensor_t *sensors[] = {&as->gyro, &as->accel};
    for (size_t i = 0; i < SDL_arraysize(sensors); i++)
    {
        AS_ZEROP(sensors[i]->timestamp);
        AS_ZEROP(sensors[i]->data);
        sensors[i]->num_samples = 0;
        SDL_zero(sensors[i]->stats);
        sensors[i]->enabled = false;
    }
#undef AS_ZEROP
}

static bool AllocAppState(appstate_t *as)
{
#define AS_MALLOC(ptr)                                        \
    {                                                         \
        (ptr) = SDL_malloc(sizeof(*(ptr)) * as->max_samples); \
        if (!(ptr))                                           \
        {                                                     \
            return false;                                     \
        }                                                     \
    }

    AS_MALLOC(as->delta_time);

    stick_t *sticks[] = {&as->left, &as->right};
    for (size_t i = 0; i < SDL_arraysize(sticks); i++)
    {
        AS_MALLOC(sticks[i]->timestamp);
        AS_MALLOC(sticks[i]->x.value);
        AS_MALLOC(sticks[i]->y.value);
    }

    sensor_t *sensors[] = {&as->gyro, &as->accel};
    for (size_t i = 0; i < SDL_arraysize(sensors); i++)
    {
        AS_MALLOC(sensors[i]->timestamp);
        AS_MALLOC(sensors[i]->data);
    }

    return true;
#undef AS_MALLOC
}

static void PrintUsage(void)
{
    SDL_Log("Usage: %s [options]", PROJECT_NAME);
    SDL_Log("Options:");
    SDL_Log("  -h, --help              Print usage information and exit.");
    SDL_Log("  -t, --time <seconds>    Number of seconds to measure input.");
    SDL_Log("  -v, --verbose           Show detailed statistics.");
    SDL_Log("  --version               Print version number and exit.");
}

static bool ReadArgs(int argc, char *argv[], appstate_t *as)
{
    for (int i = 1; i < argc;)
    {
        int count = 0;

        if (SDL_strcmp(argv[i], "-h") == 0
            || SDL_strcmp(argv[i], "--help") == 0)
        {
            PrintUsage();
            return false;
        }
        else if (SDL_strcmp(argv[i], "-t") == 0
                 || SDL_strcmp(argv[i], "--time") == 0)
        {
            if (argv[i + 1])
            {
                const int seconds = SDL_atoi(argv[i + 1]);
                as->measure_time = SDL_clamp(seconds, 0, MAX_MEASURE_TIME);
                count = 2;
            }
        }
        else if (SDL_strcmp(argv[i], "-v") == 0
                 || SDL_strcmp(argv[i], "--verbose") == 0)
        {
            as->verbose = true;
            count = 1;
        }
        else if (SDL_strcmp(argv[i], "--version") == 0)
        {
            SDL_Log("%s %s", PROJECT_NAME, PROJECT_VERSION);
            return false;
        }

        if (count == 0)
        {
            PrintUsage();
            return false;
        }

        i += count;
    }

    return true;
}

SDL_AppResult SDL_AppInit(void **appstate, int argc, char *argv[])
{
    appstate_t *as = SDL_calloc(1, sizeof(*as));
    if (as)
    {
        *appstate = (void *)as;
    }
    else
    {
        SDL_Log("Error: Memory allocation failure.");
        return SDL_APP_FAILURE;
    }

#ifdef _WIN32
    EnableVtProcessing(as);
#endif
    InitLog(as);

    if (!ReadArgs(argc, argv, as))
    {
        return SDL_APP_FAILURE;
    }

    if (as->measure_time == 0)
    {
        as->measure_time = DEFAULT_MEASURE_TIME;
    }
    as->max_samples = as->measure_time * MAX_RATE;
    as->left.x.axis_type = SDL_GAMEPAD_AXIS_LEFTX;
    as->left.y.axis_type = SDL_GAMEPAD_AXIS_LEFTY;
    as->right.x.axis_type = SDL_GAMEPAD_AXIS_RIGHTX;
    as->right.y.axis_type = SDL_GAMEPAD_AXIS_RIGHTY;

    if (!AllocAppState(as))
    {
        SDL_Log("%sError: Memory allocation failure.%s", as->red, as->end);
        return SDL_APP_FAILURE;
    }

    ResetAppState(as);

    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    if (!SDL_Init(SDL_INIT_GAMEPAD))
    {
        SDL_Log("%sError: %s%s", as->red, SDL_GetError(), as->end);
        return SDL_APP_FAILURE;
    }
    SDL_SetEventEnabled(SDL_EVENT_GAMEPAD_UPDATE_COMPLETE, true);

    return SDL_APP_CONTINUE;
}

static void CloseGamepad(appstate_t *as, SDL_JoystickID id)
{
    if (id)
    {
        SDL_Gamepad *device = SDL_GetGamepadFromID(id);
        if (device)
        {
            SDL_CloseGamepad(device);
        }

        if (id == as->gamepad.id)
        {
            SDL_Log("%sDisconnected:%s %s", as->yellow, as->end,
                    as->gamepad.name);
            ResetAppState(as);
        }
    }
}

static bool IsSwitch2Controller(uint16_t vid, uint16_t pid)
{
    if (vid == VID_NINTENDO)
    {
        switch (pid)
        {
            case PID_NINTENDO_SWITCH2_GAMECUBE_CONTROLLER:
            case PID_NINTENDO_SWITCH2_JOYCON_LEFT:
            case PID_NINTENDO_SWITCH2_JOYCON_PAIR:
            case PID_NINTENDO_SWITCH2_JOYCON_RIGHT:
            case PID_NINTENDO_SWITCH2_PRO:
                return true;

            default:
                break;
        }
    }

    return false;
}

static bool IsSwitchController(SDL_Gamepad *device, uint16_t vid, uint16_t pid)
{
    switch (SDL_GetGamepadType(device))
    {
        case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO:
        case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_LEFT:
        case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT:
        case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_PAIR:
            // Switch 2 controllers don't need special IMU handling.
            return !IsSwitch2Controller(vid, pid);

        default:
            break;
    }

    return false;
}

static void OpenGamepad(appstate_t *as, SDL_JoystickID id)
{
    if (!id)
    {
        return;
    }

    as->gamepad.device = SDL_OpenGamepad(id);
    if (!as->gamepad.device)
    {
        SDL_Log("%sError: %s%s", as->red, SDL_GetError(), as->end);
        return;
    }

    as->gamepad.id = id;
    as->gamepad.name = SDL_GetGamepadName(as->gamepad.device);
    if (as->gamepad.name == NULL)
    {
        as->gamepad.name = "Unknown Controller";
    }
    const uint16_t vid = SDL_GetGamepadVendor(as->gamepad.device);
    const uint16_t pid = SDL_GetGamepadProduct(as->gamepad.device);
    if (as->verbose)
    {
        SDL_Log("%sConnected:%s %s [VID: 0x%04x] [PID: 0x%04x]", as->blue,
                as->end, as->gamepad.name, vid, pid);
    }
    else
    {
        SDL_Log("%sConnected:%s %s", as->blue, as->end, as->gamepad.name);
    }

    as->gamepad.switch_controller =
        IsSwitchController(as->gamepad.device, vid, pid);

    as->left.enabled =
        SDL_GamepadHasAxis(as->gamepad.device, SDL_GAMEPAD_AXIS_LEFTX)
        && SDL_GamepadHasAxis(as->gamepad.device, SDL_GAMEPAD_AXIS_LEFTY);

    as->right.enabled =
        SDL_GamepadHasAxis(as->gamepad.device, SDL_GAMEPAD_AXIS_RIGHTX)
        && SDL_GamepadHasAxis(as->gamepad.device, SDL_GAMEPAD_AXIS_RIGHTY);

    as->gyro.enabled = SDL_GamepadHasSensor(as->gamepad.device, SDL_SENSOR_GYRO)
                       && SDL_SetGamepadSensorEnabled(as->gamepad.device,
                                                      SDL_SENSOR_GYRO, true);

    as->accel.enabled =
        SDL_GamepadHasSensor(as->gamepad.device, SDL_SENSOR_ACCEL)
        && SDL_SetGamepadSensorEnabled(as->gamepad.device, SDL_SENSOR_ACCEL,
                                       true);
}

static void OpenNextGamepad(appstate_t *as)
{
    SDL_JoystickID *ids = SDL_GetGamepads(NULL);
    if (ids)
    {
        OpenGamepad(as, ids[0]);
    }
    SDL_free(ids);
}

static void UpdateSensorData(const appstate_t *as, const SDL_Event *event,
                             sensor_t *sensor)
{
    if (sensor->num_samples == as->max_samples)
    {
        return;
    }

    sensor->timestamp[sensor->num_samples] = event->gsensor.timestamp;
    SDL_memcpy(sensor->data[sensor->num_samples], event->gsensor.data,
               sizeof(sensor->data[sensor->num_samples]));

    sensor->num_samples++;
}

static void UpdateStickData(appstate_t *as, const SDL_Event *event,
                            stick_t *stick)
{
    if (stick->num_samples == as->max_samples)
    {
        return;
    }

    stick->timestamp[stick->num_samples] = event->gdevice.timestamp;
    stick->x.value[stick->num_samples] =
        SDL_GetGamepadAxis(as->gamepad.device, stick->x.axis_type);
    stick->y.value[stick->num_samples] =
        SDL_GetGamepadAxis(as->gamepad.device, stick->y.axis_type);
    stick->num_samples++;
}

SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event *event)
{
    appstate_t *as = (appstate_t *)appstate;

    switch (event->type)
    {
        case SDL_EVENT_GAMEPAD_UPDATE_COMPLETE:
            if (as->state == STATE_MEASURE
                && event->gdevice.which == as->gamepad.id)
            {
                UpdateStickData(as, event, &as->left);
                UpdateStickData(as, event, &as->right);
            }
            break;

        case SDL_EVENT_GAMEPAD_SENSOR_UPDATE:
            if (as->state == STATE_MEASURE
                && event->gsensor.which == as->gamepad.id)
            {
                switch (event->gsensor.sensor)
                {
                    case SDL_SENSOR_GYRO:
                        UpdateSensorData(as, event, &as->gyro);
                        break;

                    case SDL_SENSOR_ACCEL:
                        UpdateSensorData(as, event, &as->accel);
                        break;

                    default:
                        break;
                }
            }
            break;

        case SDL_EVENT_GAMEPAD_ADDED:
            if (as->state != STATE_DONE && !as->gamepad.device)
            {
                OpenGamepad(as, event->gdevice.which);
            }
            break;

        case SDL_EVENT_GAMEPAD_REMOVED:
            if (as->state != STATE_DONE)
            {
                CloseGamepad(as, event->gdevice.which);
                if (!as->gamepad.device)
                {
                    OpenNextGamepad(as);
                }
            }
            break;

        case SDL_EVENT_QUIT:
            return SDL_APP_SUCCESS;

        default:
            break;
    }

    return SDL_APP_CONTINUE;
}

static void ClampSet(stats_set_t *set, double max_value)
{
    set->mean = SDL_clamp(set->mean, 0.0, max_value);
    set->sd = SDL_clamp(set->sd, 0.0, max_value);
    set->min = SDL_clamp(set->min, 0.0, max_value);
    set->p1 = SDL_clamp(set->p1, 0.0, max_value);
    set->p5 = SDL_clamp(set->p5, 0.0, max_value);
    set->p25 = SDL_clamp(set->p25, 0.0, max_value);
    set->p50 = SDL_clamp(set->p50, 0.0, max_value);
    set->p75 = SDL_clamp(set->p75, 0.0, max_value);
    set->p95 = SDL_clamp(set->p95, 0.0, max_value);
    set->p99 = SDL_clamp(set->p99, 0.0, max_value);
    set->max = SDL_clamp(set->max, 0.0, max_value);
}

static void ClampForResultsTable(stats_t *stats)
{
    ClampSet(&stats->interval, MAX_RESULTS_INTERVAL);
    ClampSet(&stats->rate, MAX_RESULTS_RATE);
    stats->count = SDL_min(stats->count, MAX_RESULTS_COUNT);
    stats->num_samples = SDL_min(stats->num_samples, MAX_RESULTS_COUNT);
    stats->num_duplicate = SDL_min(stats->num_duplicate, MAX_RESULTS_COUNT);
    stats->num_invalid = SDL_min(stats->num_invalid, MAX_RESULTS_COUNT);
}

static double CalcStandardDeviation(const double *data, double mean,
                                    size_t count)
{
    double squared_deviations = 0.0;

    for (size_t i = 0; i < count; i++)
    {
        const double deviation = data[i] - mean;
        squared_deviations += (deviation * deviation);
    }

    const size_t num_samples_bessel = count - 1;
    const double variance = squared_deviations / num_samples_bessel;
    const double standard_deviation = SDL_sqrt(variance);

    return standard_deviation;
}

// CDF method for calculating percentiles. 50th percentile is median.
// Langford, E. (2006). Quartiles in Elementary Statistics. Journal of
// Statistics Education, 14(3). https://doi.org/10.1080/10691898.2006.11910589
static double CalcPercentile(const double *data, size_t count,
                             size_t percentile)
{
    if (percentile == 0)
    {
        return data[0];
    }
    else if (percentile >= 100)
    {
        return data[count - 1];
    }

    const double np = count * percentile / 100.0;
    const size_t remainder = (count * percentile) % 100;

    if (remainder == 0)
    {
        const size_t index = SDL_lround(np) - 1;
        return ((data[index] + data[index + 1]) * 0.5);
    }
    else
    {
        const size_t index = SDL_lround(SDL_ceil(np)) - 1;
        return data[index];
    }
}

static int SDLCALL SortDeltaTimes(const void *a, const void *b)
{
    const double A = *(const double *)a;
    const double B = *(const double *)b;
    return ((A < B) ? -1 : (A > B));
}

static void CalcStats(double *data, size_t count, size_t num_samples,
                      size_t num_duplicate, stats_t *stats)
{
    if (count < 2)
    {
        return;
    }

    // Sample interval.
    double sum = 0.0;
    for (size_t i = 0; i < count; i++)
    {
        data[i] *= MS_PER_NS;
        sum += data[i];
    }
    stats_set_t *dt = &stats->interval;
    dt->mean = sum / count;
    dt->sd = CalcStandardDeviation(data, dt->mean, count);
    SDL_qsort(data, count, sizeof(*data), SortDeltaTimes);
    dt->min = data[0];
    dt->p1 = CalcPercentile(data, count, 1);
    dt->p5 = CalcPercentile(data, count, 5);
    dt->p25 = CalcPercentile(data, count, 25);
    dt->p50 = CalcPercentile(data, count, 50);
    dt->p75 = CalcPercentile(data, count, 75);
    dt->p95 = CalcPercentile(data, count, 95);
    dt->p99 = CalcPercentile(data, count, 99);
    dt->max = data[count - 1];

    // Sample rate.
    stats_set_t *hz = &stats->rate;
    hz->mean = MS_PER_S / dt->mean;
    hz->sd = 0.0; // Don't use inverse of standard deviation.
    for (size_t i = 0; i < count; i++)
    {
        data[i] = MS_PER_S / data[i];
    }
    SDL_qsort(data, count, sizeof(*data), SortDeltaTimes);
    hz->min = data[0];
    hz->p1 = CalcPercentile(data, count, 1);
    hz->p5 = CalcPercentile(data, count, 5);
    hz->p25 = CalcPercentile(data, count, 25);
    hz->p50 = CalcPercentile(data, count, 50);
    hz->p75 = CalcPercentile(data, count, 75);
    hz->p95 = CalcPercentile(data, count, 95);
    hz->p99 = CalcPercentile(data, count, 99);
    hz->max = data[count - 1];

    // Sample counts.
    stats->count = count;
    stats->num_samples = num_samples;
    stats->num_duplicate = num_duplicate;
    stats->num_invalid = num_samples - count - num_duplicate;

    ClampForResultsTable(stats);
}

static bool ArrayEqual(const float *a, const float *b, size_t num_values)
{
    size_t num_equal = 0;
    for (size_t i = 0; i < num_values; i++)
    {
        if (SDL_fabsf(a[i] - b[i]) < 1.0e-6f)
        {
            num_equal++;
        }
    }
    return (num_equal == num_values);
}

static void CalcSensorRate(double *delta_time, sensor_t *sensor)
{
    size_t count = 0;
    size_t num_duplicate = 0;
    uint64_t last_timestamp = sensor->timestamp[0];
    float last_data[NUM_SENSOR_AXES];
    SDL_memcpy(last_data, sensor->data[0], sizeof(last_data));

    for (size_t i = 1; i < sensor->num_samples; i++)
    {
        if (ArrayEqual(sensor->data[i], last_data, NUM_SENSOR_AXES))
        {
            num_duplicate++;
            continue;
        }

        const uint64_t interval = sensor->timestamp[i] - last_timestamp;
        if (interval >= MIN_INTERVAL && interval <= MAX_INTERVAL)
        {
            delta_time[count] = (double)interval;
            count++;
        }

        last_timestamp = sensor->timestamp[i];
        SDL_memcpy(last_data, sensor->data[i], sizeof(last_data));
    }

    CalcStats(delta_time, count, sensor->num_samples, num_duplicate,
              &sensor->stats);
}

static void CalcSensorRateSwitch(double *delta_time, sensor_t *sensor)
{
    if (sensor->num_samples < IMU_SAMPLES_DUP_WINDOW)
    {
        return;
    }

    size_t count = 0;
    size_t num_duplicate = 0;
    uint64_t last_timestamp = sensor->timestamp[IMU_SAMPLES_PER_PACKET - 1];
    size_t num_tracked = 0;
    size_t num_tracked_duplicate = 0;

    for (size_t i = IMU_SAMPLES_DUP_WINDOW - 1; i < sensor->num_samples;
         i += IMU_SAMPLES_PER_PACKET)
    {
        for (size_t j = 0; j < IMU_SAMPLES_PER_PACKET; j++)
        {
            num_tracked++;
            for (size_t k = j + 1; k < IMU_SAMPLES_DUP_WINDOW; k++)
            {
                if (ArrayEqual(sensor->data[i - j], sensor->data[i - k],
                               NUM_SENSOR_AXES))
                {
                    num_tracked_duplicate++;
                    break;
                }
            }
        }

        if (num_tracked_duplicate == num_tracked)
        {
            continue;
        }

        const size_t num_found = num_tracked - num_tracked_duplicate;
        const double interval =
            (double)(sensor->timestamp[i] - last_timestamp) / num_found;
        if (interval >= MIN_INTERVAL && interval <= MAX_INTERVAL)
        {
            for (size_t j = 0; j < num_found; j++)
            {
                delta_time[count] = interval;
                count++;
            }
        }

        last_timestamp = sensor->timestamp[i];
        num_tracked = 0;
        num_duplicate += num_tracked_duplicate;
        num_tracked_duplicate = 0;
    }

    CalcStats(delta_time, count, sensor->num_samples, num_duplicate,
              &sensor->stats);
}

static void CalcStickRate(double *delta_time, stick_t *stick)
{
    size_t count = 0;
    size_t num_duplicate = 0;
    uint64_t last_timestamp = stick->timestamp[0];
    int16_t last_stick_x = stick->x.value[0];
    int16_t last_stick_y = stick->y.value[0];

    for (size_t i = 1; i < stick->num_samples; i++)
    {
        if (stick->x.value[i] == last_stick_x
            && stick->y.value[i] == last_stick_y)
        {
            num_duplicate++;
            continue;
        }

        const uint64_t interval = stick->timestamp[i] - last_timestamp;
        if (interval >= MIN_INTERVAL && interval <= MAX_INTERVAL)
        {
            delta_time[count] = (double)interval;
            count++;
        }

        last_timestamp = stick->timestamp[i];
        last_stick_x = stick->x.value[i];
        last_stick_y = stick->y.value[i];
    }

    CalcStats(delta_time, count, stick->num_samples, num_duplicate,
              &stick->stats);
}

static void CalcPollingRate(double *delta_time, const uint64_t *timestamp,
                            size_t num_samples, stats_t *stats)
{
    size_t count = 0;
    uint64_t last_timestamp = timestamp[0];

    for (size_t i = 1; i < num_samples; i++)
    {
        const uint64_t interval = timestamp[i] - last_timestamp;
        if (interval >= MIN_INTERVAL && interval <= MAX_INTERVAL)
        {
            delta_time[count] = (double)interval;
            count++;
        }

        last_timestamp = timestamp[i];
    }

    CalcStats(delta_time, count, num_samples, 0, stats);
}

static void CalcResults(appstate_t *as)
{
    if (as->gyro.enabled)
    {
        CalcPollingRate(as->delta_time, as->gyro.timestamp,
                        as->gyro.num_samples, &as->poll);
    }
    else if (as->left.enabled)
    {
        CalcPollingRate(as->delta_time, as->left.timestamp,
                        as->left.num_samples, &as->poll);
    }
    else
    {
        CalcPollingRate(as->delta_time, as->right.timestamp,
                        as->right.num_samples, &as->poll);
    }

    if (as->left.enabled)
    {
        CalcStickRate(as->delta_time, &as->left);
    }

    if (as->right.enabled)
    {
        CalcStickRate(as->delta_time, &as->right);
    }

    if (as->gamepad.switch_controller)
    {
        if (as->gyro.enabled)
        {
            CalcSensorRateSwitch(as->delta_time, &as->gyro);
        }

        if (as->accel.enabled)
        {
            CalcSensorRateSwitch(as->delta_time, &as->accel);
        }
    }
    else
    {
        if (as->gyro.enabled)
        {
            CalcSensorRate(as->delta_time, &as->gyro);
        }

        if (as->accel.enabled)
        {
            CalcSensorRate(as->delta_time, &as->accel);
        }
    }
}

static void ShowResults(appstate_t *as)
{
    CalcResults(as);

    const bool enabled[] = {
        true,
        as->left.enabled,
        as->right.enabled,
        as->gyro.enabled,
        as->accel.enabled,
    };
    const char *names[] = {
        as->verbose ? "Polling" : "Polling Rate",
        as->left.enabled && as->right.enabled ? "Left Stick" : "Stick",
        as->left.enabled && as->right.enabled ? "Right Stick" : "Stick",
        "IMU Gyro",
        "IMU Accel",
    };
    const stats_t *all_stats[] = {
        &as->poll,       &as->left.stats,  &as->right.stats,
        &as->gyro.stats, &as->accel.stats,
    };
    const size_t num_rows = SDL_arraysize(all_stats);

    if (as->verbose)
    {
        SDL_Log("");
        SDL_Log("%s────────────────────────────────────────────────────────────"
                "──────────────────%s",
                as->gray, as->end);
        SDL_Log(" %s%-13s%s %6s %s%6s %6s %6s%s %6s %s%6s %6s %6s %6s%s",
                as->blue, "Interval [ms]", as->end, "Mean", as->gray, "SD",
                "P1", "P25", as->end, "P50", as->gray, "P75", "P99", "N",
                "Total", as->end);
        SDL_Log("%s────────────────────────────────────────────────────────────"
                "──────────────────%s",
                as->gray, as->end);
        for (size_t i = 0; i < num_rows; i++)
        {
            const stats_t *stats = all_stats[i];
            const stats_set_t *set = &stats->interval;
            if (enabled[i])
            {
                SDL_Log(" %-13s %6.3f %s%6.3f %6.3f %6.3f%s %6.3f %s%6.3f "
                        "%6.3f %6d %6d%s",
                        names[i], set->mean, as->gray, set->sd, set->p1,
                        set->p25, as->end, set->p50, as->gray, set->p75,
                        set->p99, stats->count, stats->num_samples, as->end);
            }
        }
        SDL_Log("%s────────────────────────────────────────────────────────────"
                "──────────────────%s",
                as->gray, as->end);
        SDL_Log("");
        SDL_Log("%s────────────────────────────────────────────────────────────"
                "──────────────────%s",
                as->gray, as->end);
        SDL_Log(" %s%-13s%s %6s %s%6s %6s %6s%s %6s %s%6s %6s %6s %6s%s",
                as->blue, "Rate [Hz]", as->end, "Mean", as->gray, "", "P1",
                "P25", as->end, "P50", as->gray, "P75", "P99", "N", "Total",
                as->end);
        SDL_Log("%s────────────────────────────────────────────────────────────"
                "──────────────────%s",
                as->gray, as->end);
        for (size_t i = 0; i < num_rows; i++)
        {
            const stats_t *stats = all_stats[i];
            const stats_set_t *set = &stats->rate;
            if (enabled[i])
            {
                SDL_Log(" %-13s %6.0f %s%6s %6.0f %6.0f%s %6.0f %s%6.0f %6.0f "
                        "%6d %6d%s",
                        names[i], set->mean, as->gray, "", set->p1, set->p25,
                        as->end, set->p50, as->gray, set->p75, set->p99,
                        stats->count, stats->num_samples, as->end);
            }
        }
        SDL_Log("%s────────────────────────────────────────────────────────────"
                "──────────────────%s",
                as->gray, as->end);
    }
    else
    {
        SDL_Log("%23s %10s", "Average", "Median");
        for (size_t i = 0; i < num_rows; i++)
        {
            if (enabled[i])
            {
                SDL_Log("%-12s %7.0f Hz %7.0f Hz", names[i],
                        all_stats[i]->rate.mean, all_stats[i]->rate.p50);
            }
        }
    }
}

static float ScaleAxis(int16_t value)
{
    return ((float)value / (value > 0 ? SDL_MAX_SINT16 : -SDL_MIN_SINT16));
}

static bool MovedStick(appstate_t *as)
{
    const stick_t *sticks[] = {&as->left, &as->right};
    for (size_t i = 0; i < SDL_arraysize(sticks); i++)
    {
        const float x = ScaleAxis(
            SDL_GetGamepadAxis(as->gamepad.device, sticks[i]->x.axis_type));
        const float y = ScaleAxis(
            SDL_GetGamepadAxis(as->gamepad.device, sticks[i]->y.axis_type));
        const float magnitude = SDL_sqrtf(x * x + y * y);
        if (magnitude > 0.5f)
        {
            return true;
        }
    }
    return false;
}

SDL_AppResult SDL_AppIterate(void *appstate)
{
    const uint64_t now = SDL_GetTicksNS();
    appstate_t *as = (appstate_t *)appstate;

    if (as->last_time == 0)
    {
        as->last_time = now;
    }

    if (now - as->last_time >= as->interval)
    {
        as->last_time = now;

        switch (as->state)
        {
            case STATE_START:
                as->interval = SDL_NS_PER_SECOND;
                as->state = STATE_CONNECT;
                if (!as->gamepad.device)
                {
                    SDL_Log("Waiting for a controller...");
                    break;
                }
                // Fall through.

            case STATE_CONNECT:
                if (!as->gamepad.device)
                {
                    break;
                }
                else if (!as->left.enabled && !as->right.enabled)
                {
                    SDL_Log("%sError: No analog sticks found.%s", as->red,
                            as->end);
                    CloseGamepad(as, as->gamepad.id);
                    break;
                }
                SDL_Log("Rotate stick%s for %d seconds.",
                        as->left.enabled && as->right.enabled ? "s" : "",
                        as->measure_time);
                as->state = STATE_WAIT;
                // Fall through.

            case STATE_WAIT:
                if (MovedStick(as))
                {
                    as->measure_elapsed = 0;
                    SDL_Log("%d...", as->measure_time);
                    as->state = STATE_MEASURE;
                }
                break;

            case STATE_MEASURE:
                as->measure_elapsed++;
                if (as->measure_elapsed < as->measure_time)
                {
                    SDL_Log("%d...", as->measure_time - as->measure_elapsed);
                    break;
                }
                ShowResults(as);
                as->state = STATE_DONE;
                // Fall through.

            case STATE_DONE:
                return SDL_APP_SUCCESS;

            default:
                break;
        }
    }

    return SDL_APP_CONTINUE;
}

static void FreeAppState(appstate_t *as)
{
    if (as)
    {
        SDL_free(as->delta_time);

        stick_t *sticks[] = {&as->left, &as->right};
        for (size_t i = 0; i < SDL_arraysize(sticks); i++)
        {
            SDL_free(sticks[i]->timestamp);
            SDL_free(sticks[i]->x.value);
            SDL_free(sticks[i]->y.value);
        }

        sensor_t *sensors[] = {&as->gyro, &as->accel};
        for (size_t i = 0; i < SDL_arraysize(sensors); i++)
        {
            SDL_free(sensors[i]->timestamp);
            SDL_free(sensors[i]->data);
        }

        SDL_free(as);
    }
}

void SDL_AppQuit(void *appstate, SDL_AppResult result)
{
    appstate_t *as = (appstate_t *)appstate;

#ifdef _WIN32
    // Restore console mode.
    if (as && as->hconsole)
    {
        SetConsoleMode(as->hconsole, as->original_mode);
    }
#endif

    if (as && as->gamepad.device)
    {
        SDL_CloseGamepad(as->gamepad.device);
    }

    FreeAppState(as);
    SDL_QuitSubSystem(SDL_INIT_GAMEPAD);
    SDL_Quit();

#ifdef _WIN32
    // Pause if this is the only process attached to the console.
    DWORD process_list[1];
    DWORD process_count = 1;
    if (GetConsoleProcessList(process_list, process_count) == 1)
    {
        SDL_Log("");
        system("pause");
    }
#else
    SDL_Log("");
#endif
}
