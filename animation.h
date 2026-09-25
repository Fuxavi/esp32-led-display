#ifndef ANIMATION_H
#define ANIMATION_H

#include <Arduino.h>
#include <SD.h>

// ============================================================
// Variables de la animación
// ============================================================

extern File animationFile;

extern uint8_t* animationBuffer;

extern uint16_t animationWidth;
extern uint16_t animationHeight;
extern uint16_t animationNumFrames;
extern uint16_t animationFPS;

extern size_t animationFrameSize;

extern uint16_t animationCurrentFrame;

extern uint32_t animationLastFrameTime;
extern uint32_t animationFrameDelay;
extern bool usingLedDisplay;

extern bool animationPlaying;

void startAnimation(String filename, bool ledDisplay = true);
void updateAnimation();
void stopAnimation();
bool startAudio(String filename);
bool initAudio();
void updateAudio();

#endif