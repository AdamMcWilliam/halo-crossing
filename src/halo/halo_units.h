/* Halo CE simulation constants that are compiled into the engine (not tag
 * data). Sources: halocea src/headers/game_time_constants.h (tick rate),
 * src/hcex/hcex_conv_pos.cpp (world unit), src/data/global_gravity.c. */
#ifndef HALO_UNITS_H
#define HALO_UNITS_H

#define HALO_TICKS_PER_SECOND 30
#define HALO_SECONDS_PER_TICK (1.0f / 30.0f)

/* 1 world unit = 10 feet. */
#define HALO_METERS_PER_WORLD_UNIT 3.048f

/* global_gravity is 0.00356517918 wu/tick^2, i.e. 3.2087 wu/s^2 = 9.78 m/s^2. */
#define HALO_GLOBAL_GRAVITY_PER_TICK2 0.00356517918f
#define HALO_GRAVITY (HALO_GLOBAL_GRAVITY_PER_TICK2 * HALO_TICKS_PER_SECOND * HALO_TICKS_PER_SECOND)

/* Fixed-step guard: never run more than this many ticks per host frame. */
#define HALO_MAX_TICKS_PER_FRAME 4

#endif
