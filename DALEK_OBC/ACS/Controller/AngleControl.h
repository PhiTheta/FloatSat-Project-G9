/*
 * AngleControl.h
 *
 *  Created on: Dec 31, 2015
 *      Author: arthur
 */

#ifndef ACS_CONTROLLER_ANGLECONTROL_H_
#define ACS_CONTROLLER_ANGLECONTROL_H_

#include "../../Basic/basic.h"
#include "../../Hardware/Motor.h"
extern "C" Motor motor;

class AngleControl : public Thread {
public:
	AngleControl();
	virtual ~AngleControl();
	void init();
	void run();
	bool isActive();
	void setActive(bool _val);
	void setDesAngle(float _val);
	void setNewData(IMU_RPY_FILTERED);
	void setNewData(VAR_CONTROL *_val);
private:
	float PID(float setPoint, float feedback);
	bool active;
	float desAng;
	float heading;
	float error;
	float lastError;
	float controlOut;
	float pidOut;

	float pPart;
	float pGain;
	float iPart;
	float iGain;
	float dPart;
	float dGain;
	float Ts;


	float U_1;
	float e;
	float e_1;
	float e_2;
	float a,b,c;

	float i;
	float dt;
	float period;
	float lastTime;
	CommBuffer<IMU_RPY_FILTERED> imuData;



};

#endif /* ACS_CONTROLLER_ANGLECONTROL_H_ */
