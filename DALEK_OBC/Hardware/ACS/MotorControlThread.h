/*
 * controlThread.h
 *
 *  Created on: Dec 1, 2015
 *      Author: arthur
 */

#ifndef HARDWARE_ACS_MOTORCONTROLTHREAD_H_
#define HARDWARE_ACS_MOTORCONTROLTHREAD_H_

#include "../../Basic/basic.h"
#include "../ACS/Motor.h"


class MotorControlThread :public Thread{
public:
	MotorControlThread();
	virtual ~MotorControlThread();
	void init();
	void run();
	void setMotorSpeed(float _speedCycle);
	void setMotor(bool _val);
private:
	Motor motor;
	int16_t currentDutyCycle;
};

#endif /* HARDWARE_ACS_MOTORCONTROLTHREAD_H_ */