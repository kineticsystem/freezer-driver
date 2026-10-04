# The pattern on the 16 outputs of the Freezer board, as the node knows it:
# what set_outputs latched, the step of a shot as the controller reports it,
# or all off after a shot and a stop. During a shot, the node learns each step
# when it polls the controller, so a step shorter than the polling period may
# never be published; the steps and their exact times are in freezer_msgs/Shot.

builtin_interfaces/Time stamp

# One bit per optocoupler, as in freezer_msgs/Step.
uint16 outputs

# The shot running, 0 outside a shot.
uint16 shot_id

# The step of that shot.
uint8 step
