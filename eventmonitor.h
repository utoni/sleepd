#define MAX_CHANNELS 64
struct event_data
{
	char events[MAX_CHANNELS][128];
	int channels[MAX_CHANNELS];
	int emactivity;
};

struct event_data eventData;

extern void *eventMonitor();

extern pthread_mutex_t activity_mutex;
extern pthread_mutex_t condition_mutex;
extern pthread_cond_t condition_cond;
