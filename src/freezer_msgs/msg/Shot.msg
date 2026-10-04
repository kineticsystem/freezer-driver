# A shot, when it starts and when it ends: the table it runs and when the
# controller started it, so that a client can draw it with the exact durations
# the controller counts.

uint8 STARTED = 0  # The controller has started the shot.
uint8 ENDED = 1    # The shot ran to its end.
uint8 STOPPED = 2  # The shot was stopped, after elapsed_us.
uint8 FAILED = 3   # The node lost the shot, e.g. the controller reset; see message.
uint8 event

uint8 SOURCE_NODE = 0     # A goal of the Shoot action.
uint8 SOURCE_TRIGGER = 1  # The remote trigger, IN1, seen by the node's polling.
uint8 source

# The id the controller gave the shot.
uint16 shot_id

# Host time when the shot started. For a shot of the trigger, estimated from
# the time the controller reports it has run.
builtin_interfaces/Time started

# The steps of the shot, empty when the node does not know them.
freezer_msgs/Step[] steps

# Duration of the table.
uint32 duration_us

# How long the shot ran: duration_us when it ended, less when it was stopped.
# Valid once it is over.
uint32 elapsed_us

# The latest a step boundary came after its time, as measured by the
# controller. Valid when ENDED.
uint32 worst_lateness_us

# Why the shot failed, empty otherwise.
string message
