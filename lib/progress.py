import math
import sys
from terminal import colorize

def progress_bar(progress: float, width=25, msg="", stream=sys.stdout):
    progress = min(1., max(0., progress))

    fancy_bar = True
    filled_slots = math.floor(width * progress)
    lsep = colorize("[", "yellow")
    rsep = colorize("]", "yellow")
    if filled_slots==width:
        if fancy_bar:
            bar = colorize("≡"*width, "green")
        else:
            bar = colorize("="*width, "green")
    else:
        part_slot = (width * progress) - filled_slots
        empty_slots = width - (filled_slots+1)
        if fancy_bar:
            part_width = math.floor(part_slot * 3)
            part_char = [" ", "-", "="][part_width]
            bar = colorize("≡"*filled_slots + part_char, "green") + " "*empty_slots
        else:
            part_width = math.floor(part_slot * 2)
            part_char = [" ", "-"][part_width]
            bar = colorize("="*filled_slots + part_char, "green") + " "*empty_slots

    bar = lsep + bar + rsep

    pc_str = f"{int(100*progress):3d}%"

    # Use '\r' to return to the start of the line
    stream.write('\r  ' + bar + " " + pc_str + " " + msg)
    stream.flush()
