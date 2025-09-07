#ifndef GUARD_GAME_CORNER_EXPANSION_H
#define GUARD_GAME_CORNER_EXPANSION_H

#define GAME_CORNER_VAR_ID_CHECK        0  // This is just a funny variable used for the Game Corner map itself and can be set to 0
#define GAME_CORNER_VAR_WINNINGS        VAR_GAME_CORNER_WINNINGS   // Must be set to a valid variable for most minigames to function

#define FLAPPY_VAR_HIGH_SCORE           VAR_GAME_CORNER_FLAPPYHS  // If this variable isn't set to 0, this tracks the high score of the Flappy Bird minigame

#define DERBY_FLAG_NICKNAME             FLAG_DERBY_NICKNAME    // This flag tracks whether nicknames should be reset or not. Must be set to a valid flag
#define DERBY_FLAG_RESET                FLAG_DERBY_RESET    // This flag tracks whether the data for the derby should be reset or not. Must be set to a valid flag
#define DERBY_VAR_RACER_NAME_1          VAR_DERBY_RACER_NAME_1   // The following 6 variables track the data for each indivial racer in their respective variable
#define DERBY_VAR_RACER_NAME_2          VAR_DERBY_RACER_NAME_2   // Species is stored in the hundreds place in decimal
#define DERBY_VAR_RACER_NAME_3          VAR_DERBY_RACER_NAME_3   // Shininess is stored in the tens place in decimal
#define DERBY_VAR_RACER_NAME_4          VAR_DERBY_RACER_NAME_4   // Condition is stored in the ones place in decimal
#define DERBY_VAR_RACER_NAME_5          VAR_DERBY_RACER_NAME_5
#define DERBY_VAR_RACER_NAME_6          VAR_DERBY_RACER_NAME_6
#define DERBY_VAR_RACER_1               VAR_DERBY_RACER_1   // The following 6 variables hold the nickname ID for each racer in the derby
#define DERBY_VAR_RACER_2               VAR_DERBY_RACER_2   // These variables only hold the ID itself, the actual nickname uses the species data as well
#define DERBY_VAR_RACER_3               VAR_DERBY_RACER_3
#define DERBY_VAR_RACER_4               VAR_DERBY_RACER_4
#define DERBY_VAR_RACER_5               VAR_DERBY_RACER_5
#define DERBY_VAR_RACER_6               VAR_DERBY_RACER_6

#define FLIP_VAR_LEVEL                  VAR_VOLTORB_FLIP_LEVEL   // If this variable isn't set to 0, it will track the difficult level for any game of Voltorb Flip after the first one

#endif // GUARD_GAME_CORNER_EXPANSION_H
