/*
 * IMU.cpp
 *
 *  Created on: Oct 31, 2015
 *      Author: arthur
 */


#include "IMU.h"
#include <inttypes.h>

//#include <unistd.h>

#define IMU_GYRO	0
#define IMU_ACCMAG	1

static Application senderName("IMU_Data_Publisher",500);


HAL_GPIO imu_g_cs(IMU_G_CS_PIN);
HAL_GPIO imu_x_cs(IMU_XM_CS_PIN);


HAL_GPIO reset(IMU_RESET_PIN);
HAL_I2C i2c2(I2C_IDX2);

int samples = 0;



IMU::IMU() {
	//TODO: insert correct baudrate
}

IMU::~IMU() {
	// TODO Auto-generated destructor stub
}


void IMU::init(){
	PRINTF("IMU Constructor called!\n");
	cnt_failedReads = 0;
	samples = 0;
	int k =0;
	oldSamplerateTime = 0.0;
	samplerateTime = 0.0;
	cosFactor = 0.0;
	deltaYaw = 0.0;
	deltaPitch = 0.0;
	deltaRoll = 0.0;
	//	uint8_t time = 500*MILLISECONDS;

	/** SPI STUFF */
	//	k = spi2.init(1250000);
	//	PRINTF("init: %d\n",k);
	// setting SPI Mode:
	//	k = spi2.config(SPI_PARAMETER_MODE,1);
	//	PRINTF("config: %d\n",k);
	//init i2c
	i2c2.init(400000);

	//init array
	memset(recBuf,0,sizeof(recBuf));
	memset(transBuf,0,sizeof(transBuf));
	memset(gyro_raw,0,sizeof(gyro_raw));
	memset(accl_raw,0,sizeof(accl_raw));
	memset(magn_raw,0,sizeof(magn_raw));
	memset(temp_raw,0,sizeof(temp_raw));
	//init GPIOs
	imu_g_cs.init(true,1,1);
	imu_x_cs.init(true,1,1);
	reset.init(true,1,1);
	//init offsets
	memset(gyroOffset,IMU_GYRO_DEFAULT_OFFSET,sizeof(gyroOffset));
	memset(acclOffset,IMU_ACCL_DEFAULT_OFFSET,sizeof(acclOffset));
	memset(magnOffset,IMU_MAGN_DEFAULT_OFFSET,sizeof(magnOffset));

	/** WHOIS CHECKS *************************************************************** */
	imu_g_cs.setPins(1);
	k=imu_g_cs.readPins();
	PRINTF("IMU G CS PIN:%d\n",k);
	transBuf[0] = (0x80 | (WHO_AM_I_GYRO));
	k = i2c2.writeRead(GYRO_ADDRESS,transBuf,1,recBuf,1);
	//	k = spi2.writeRead(transBuf,1,recBuf,30);
	PRINTF("whois gyro: k=%d -  %d\n",k,recBuf[0]); // should be 212 ,0xD4
	imu_g_cs.setPins(0);
	imu_x_cs.setPins(1);
	transBuf[0] = ( 0x80 | WHO_AM_I_MAGNACC);
	k = i2c2.writeRead(ACC_MAG_ADDRESS,transBuf,1,recBuf,1);
	//	k = spi2.writeRead(transBuf,1,recBuf,2);
	PRINTF("whois accMag: k=%d  -  %d\n",k,recBuf[0]); // should be 73 ,0x49
	imu_x_cs.setPins(0);


	/** ACCELEROMETER, MAGNETIC SENSOR AND TEMP SETUP *********************************************/
	imu_x_cs.setPins(1);
	transBuf[0] = (CTRL_REG5_XM);
	transBuf[1] = 0x94; // -> enable temp readings, set high resolution magnetometer, read frequency mag 100Hz
	k = i2c2.write(ACC_MAG_ADDRESS,transBuf,2);
	//	k = spi2.write(transBuf,2);
	//	PRINTF("k-val: %d\n",k);
	//enable Accelerometer
	transBuf[0] = (CTRL_REG1_XM);
	transBuf[1] = 0xA7;//0b10100111 -> 1600Hz, continuous reading, all axes enabled
	k = i2c2.write(ACC_MAG_ADDRESS,transBuf,2);
	//	k = spi2.write(transBuf,2);
	//	PRINTF("k-val 2: %d\n",k);
	// setting Magnetic sensor mode to continuous conversion mode
	transBuf[0] = CTRL_REG7_XM;
	transBuf[1] = 0x00; //0b00000000 -> continuous conversion mode and rest as default
	k = i2c2.write(ACC_MAG_ADDRESS,transBuf,2);
	//	 setting range
	transBuf[0] = CTRL_REG2_XM;
	switch(IMU_ACCL_RANGE){
	case 2:
		transBuf[1] = ACCL_2G;
		acclSensitivity = ACCL_2G_SENSITIVITY;
		break;
	case 4:
		transBuf[1] = ACCL_4G;
		acclSensitivity = ACCL_4G_SENSITIVITY;
		break;
	case 6:
		transBuf[1] = ACCL_6G;
		acclSensitivity = ACCL_6G_SENSITIVITY;
		break;
	case 8:
		transBuf[1] = ACCL_8G;
		acclSensitivity = ACCL_8G_SENSITIVITY;
		break;
	case 16:
		transBuf[1] = ACCL_16G;
		acclSensitivity = ACCL_16G_SENSITIVITY;
		break;
	default:
		PRINTF("WRONG ACCL SCALE DEFINITION IN BASIC.H\n suspending IMU Thread...\n");
		suspendCallerUntil(END_OF_TIME);
		break;
	}
	k = i2c2.write(ACC_MAG_ADDRESS,transBuf,2);

	//now setting magnetic range
	imu_x_cs.setPins(1);
	transBuf[0] = CTRL_REG6_XM;
	switch(IMU_MAGN_RANGE){
	case 2:
		transBuf[1] = MAGN_2GAUSS;
		magnSensitivity = MAGN_2GAUSS_SENSITIVITY;
		break;
	case 4:
		transBuf[1] = MAGN_4GAUSS;
		magnSensitivity = MAGN_4GAUSS_SENSITIVITY;
		break;
	case 8:
		transBuf[1] = MAGN_8GAUSS;
		magnSensitivity = MAGN_8GAUSS_SENSITIVITY;
		break;
	case 12:
		transBuf[1] = MAGN_12GAUSS;
		magnSensitivity = MAGN_12GAUSS_SENSITIVITY;
		break;
	default:
		PRINTF("WRONG MAGN SCALE DEFINITION IN BASIC.H\n suspending IMU Thread...\n");
		suspendCallerUntil(END_OF_TIME);
		break;
	}
	k = i2c2.write(ACC_MAG_ADDRESS,transBuf,2);

	imu_x_cs.setPins(0);




	/** GYRO SETTINGS *********************************************** */
	imu_g_cs.setPins(1);
	transBuf[0] = CTRL_REG1_G;
	transBuf[1] = 0xCF; //0b11001111 Normal power mode, all axes enabled,  760Hz, 30 cutoff
	i2c2.write(GYRO_ADDRESS,transBuf,2);
	transBuf[0] = CTRL_REG4_G;
	PRINTF("Setting Gyro Scale to %d\n",IMU_GYRO_RANGE);
	switch(IMU_GYRO_RANGE){
	case 245:
		transBuf[1] = GYRO_245DPS;
		gyroSensitivity = GYRO_245DPS_SENSITIVITY;
		break;
	case 500:
		transBuf[1] = GYRO_500DPS;
		gyroSensitivity = GYRO_500DPS_SENSITIVITY;
		break;
	case 2000:
		transBuf[1] = GYRO_2000DPS;
		gyroSensitivity = GYRO_2000DPS_SENSITIVITY;
		break;
	default:
		PRINTF("WRONG GYRO SCALE DEFINITION IN BASIC.H\nsuspending IMU Thread...\n");
		suspendCallerUntil(END_OF_TIME);
		break;
	}

	i2c2.write(GYRO_ADDRESS,transBuf,2);
	imu_g_cs.setPins(1);
	//
	//
	//	//check data
	imu_g_cs.setPins(1);
	transBuf[0] =(0x80 | CTRL_REG1_G);
	k = i2c2.writeRead(GYRO_ADDRESS,transBuf,1,recBuf,1);
	//	k = spi2.writeRead(transBuf,1,recBuf,2);
	PRINTF("got k=%d  -  GYRO REG1:%d\n",k,recBuf[0]); // should be 0xCf -> 207
	imu_g_cs.setPins(0);





}


//TODO retcodes
int IMU::resetIMU(){
	//cycle PD7 off->on for reset //TODO: but how fast???
	//	leds.blinkAll(100,0);
	suspendCallerUntil(NOW()+200*MILLISECONDS);

	reset.setPins(0);
	reset.setPins(1);
	return 0;
}


IMU_DATA_RAW IMU::scaleData(){
	IMU_DATA_RAW tmp;
	tmp.TEMP_RAW = temp_raw[0];
	tmp.ANGULAR_RAW_X = (gyro_raw[0] - gyroOffset[0])* gyroSensitivity;
	tmp.ANGULAR_RAW_Y = (gyro_raw[1] - gyroOffset[1])* gyroSensitivity;
	tmp.ANGULAR_RAW_Z = (gyro_raw[2] - gyroOffset[2])* gyroSensitivity;
	tmp.ACCEL_RAW_X = (accl_raw[0] - acclOffset[0])* acclSensitivity;
	tmp.ACCEL_RAW_Y = (accl_raw[1] - acclOffset[1])* acclSensitivity;
	tmp.ACCEL_RAW_Z = (accl_raw[2] - acclOffset[2])* acclSensitivity;
	tmp.MAGNETIC_RAW_X = (magn_raw[0] - magnOffset[0])* magnSensitivity;
	tmp.MAGNETIC_RAW_Y = (magn_raw[1] - magnOffset[1])* magnSensitivity;
	tmp.MAGNETIC_RAW_Z = (magn_raw[2] - magnOffset[2])* magnSensitivity;
	//convert gyro data to rad per seconds (for later caluclation stuff...)
	tmp.ANGULAR_RAW_X *= TO_RAD;
	tmp.ANGULAR_RAW_Y *= TO_RAD;
	tmp.ANGULAR_RAW_Z *= TO_RAD;
	return tmp;
}

IMU_DATA_RAW IMU::readIMU_Data(){
	int k = 0;
#ifdef AUTO_RESET_IMU
	oldData = newData;
#endif
	k = read_multiple_Register(IMU_GYRO,(X_ANGULAR_L),6,gyro_raw);
	k = read_multiple_Register(IMU_ACCMAG,X_ACCEL_L,6,accl_raw);
	k = read_multiple_Register(IMU_ACCMAG,X_MAGNETIC_L,6,magn_raw);
	k = read_multiple_Register(IMU_ACCMAG,TEMP_L,2,temp_raw);

//	PRINTF("\nraw Magdata  %f  %f  %f",magn_raw[0],magn_raw[1],magn_raw[2]);

	newData = scaleData();
	samples++;
	samplerateTime = SECONDS_NOW();
	PRINTF("\nSamples: %d\nGYRO:   %f   %f   %f  degree/sec\nACCL:   %f   %f   %f   G\nMAGN:   %f   %f   %f   gauss\n",samples,newData.ANGULAR_RAW_X,newData.ANGULAR_RAW_Y,newData.ANGULAR_RAW_Z,newData.ACCEL_RAW_X,newData.ACCEL_RAW_Y,newData.ACCEL_RAW_Z,newData.MAGNETIC_RAW_X,newData.MAGNETIC_RAW_Y,newData.MAGNETIC_RAW_Z);

#ifdef AUTO_RESET_IMU
	//check for hangs on each channel -> if hang, try to reset IMU
	/** TODO Check how to properly compare floating point values! */
	if((fabsf(oldData.ACCEL_RAW_X - newData.ACCEL_RAW_X) < EPSILON_COMPARISON) &&
			(fabsf(oldData.ACCEL_RAW_Y - newData.ACCEL_RAW_Y) <EPSILON_COMPARISON)&&
			(fabsf(oldData.ACCEL_RAW_Z - newData.ACCEL_RAW_Z) < EPSILON_COMPARISON)) cnt_failedReads++;
	if((fabsf(oldData.ANGULAR_RAW_X - newData.ANGULAR_RAW_X) < EPSILON_COMPARISON) &&
			(fabsf(oldData.ANGULAR_RAW_Y - newData.ANGULAR_RAW_Y) < EPSILON_COMPARISON) &&
			(fabsf(oldData.ANGULAR_RAW_Z - newData.ANGULAR_RAW_Z) < EPSILON_COMPARISON)) cnt_failedReads++;
	if((fabsf(oldData.MAGNETIC_RAW_X - newData.MAGNETIC_RAW_X) < EPSILON_COMPARISON) &&
			(fabsf(oldData.MAGNETIC_RAW_Y - newData.MAGNETIC_RAW_Y) < EPSILON_COMPARISON) &&
			(fabsf(oldData.MAGNETIC_RAW_Z - newData.MAGNETIC_RAW_Z) < EPSILON_COMPARISON)) cnt_failedReads++;
	if(cnt_failedReads > RESET_IMU_AFTER){
		PRINTF("IMU Hang detected! Resetting IMU\n");
		this->resetIMU();
		init();
	}
#endif
}

void IMU::convertToRPY(){
	cosFactor = 1/(cosf(angleRPY.GYRO_PITCH));
	deltaPitch = cosFactor * ((cosf(angleRPY.GYRO_ROLL) * cosf(angleRPY.GYRO_PITCH)*newData.ANGULAR_RAW_Y) - (sinf(angleRPY.GYRO_ROLL)*cosf(angleRPY.GYRO_PITCH)*(newData.ANGULAR_RAW_Z )));
	deltaRoll = cosFactor * ((cosf(angleRPY.GYRO_PITCH) * (newData.ANGULAR_RAW_X)) + (sinf(angleRPY.GYRO_ROLL)*sinf(angleRPY.GYRO_PITCH)*(newData.ANGULAR_RAW_Y)) + (cosf(angleRPY.GYRO_ROLL)*sin(angleRPY.GYRO_PITCH)*(newData.ANGULAR_RAW_Z )));
	deltaYaw = cosFactor * ((sinf(angleRPY.GYRO_ROLL) * (newData.ANGULAR_RAW_Y )) + (cosf(angleRPY.GYRO_ROLL)*(newData.ANGULAR_RAW_Z )));
	float sampleDiff = samplerateTime - oldSamplerateTime;
	angleRPY.GYRO_YAW += (deltaYaw*sampleDiff);
	angleRPY.GYRO_PITCH += (deltaPitch*sampleDiff);
	angleRPY.GYRO_ROLL += (deltaRoll*sampleDiff);
//	PRINTF("\ndifference sample:   %f   \n",sampleDiff);
	oldSamplerateTime = samplerateTime;
	PRINTF("\nYAW:  %f  PITCH:   %f   ROLL:   %f   ",angleRPY.GYRO_YAW*TO_DEG,angleRPY.GYRO_PITCH*TO_DEG,angleRPY.GYRO_ROLL*TO_DEG);
//	PRINTF("\ndeltaYaw:   %f   deltaPitch:   %f   deltaRoll:   %f   \n",deltaYaw,deltaPitch,deltaRoll);

	// acclererometer convert to RPY
	angleRPY.ACCL_YAW = atan(newData.ACCEL_RAW_Z/(sqrt((newData.ACCEL_RAW_X*newData.ACCEL_RAW_X) + (newData.ACCEL_RAW_Z*newData.ACCEL_RAW_Z))));
	angleRPY.ACCL_PITCH = atan(newData.ACCEL_RAW_X/(sqrt((newData.ACCEL_RAW_Y*newData.ACCEL_RAW_Y) + (newData.ACCEL_RAW_Z*newData.ACCEL_RAW_Z))));
	angleRPY.ACCL_ROLL = atan(newData.ACCEL_RAW_Y/(sqrt((newData.ACCEL_RAW_X*newData.ACCEL_RAW_X) + (newData.ACCEL_RAW_Z*newData.ACCEL_RAW_Z))));
	PRINTF("\nYAW:   %f   PITCH:   %f   ROLL:   %f   \n",angleRPY.ACCL_YAW*TO_DEG,angleRPY.ACCL_PITCH*TO_DEG,angleRPY.ACCL_ROLL*TO_DEG);
}

/**
 * function reads multiple register (at least 2)
 * return
 * 			0 everything went well
 * 			-1 minimum values to read are 2!
 * 			-2 values to read must be an even number! (because complements of high and low bytes are read)
 * 			-3 destination array does not match number of values to read divided by two!
 */
/** TODO destination size check! */
int IMU::read_multiple_Register(int cs,uint8_t reg,int valuesToRead, int16_t *dest){
	if(valuesToRead < 2) return -1;
	if(!(valuesToRead%2 == 0)) return -2;

	// select register and set to read
	transBuf[0] = (0x80 | (reg & 0x3F));
	int j = 0;

	if(cs == IMU_GYRO){
		imu_g_cs.setPins(1);
		i2c2.writeRead(GYRO_ADDRESS,transBuf,1,recBuf,valuesToRead);
		for(int i=0;i<valuesToRead;i+=2){
			//			PRINTF("recBufGyro %d:  %d, %d\n",i,recBuf[i],recBuf[i+1]);
			dest[j] =(int16_t)(recBuf[i] | (recBuf[i+1] << 8));
			//			PRINTF("converted:%d",(int16_t)(recBuf[i] | (recBuf[i+1] << 8)));
			j++;
		}

		imu_g_cs.setPins(0);
	}else{
		imu_x_cs.setPins(1);

		i2c2.writeRead(ACC_MAG_ADDRESS,transBuf,1,recBuf,6);

		imu_x_cs.setPins(0);
		if(dest == temp_raw){
			temp_raw[0] = (((int16_t) recBuf[1] << 12) | recBuf[0] << 4 ) >> 4; // temperature is signed 12bit integer
		}else {
			for(int i=0;i<valuesToRead;i+=2){
//							PRINTF("recBufMag %d:  %d, %d\n",i,recBuf[i],recBuf[i+1]);
				dest[j] =(int16_t)(recBuf[i] | (recBuf[i+1] << 8));
				j++;
			}
		}

	}
	return 0;

}




void IMU::setTime(int time){
	this->time = time;
	PRINTF("setting period time to %d\n",time);
}

// setter functions
void IMU::setGyroScale(int scale){

}


void IMU::calibrateSensors(){
	// led command -1 nothing, 0 rotate, 1 blink, 2 on (given LEDs)
//	suspendCallerUntil(NOW()+100*MILLISECONDS);
//	setLEDMask(1,1,1,1,1);
//	led_switch.publish(led);
//	suspendCallerUntil(NOW()+100*MILLISECONDS);
//
//	setLEDMask(2,1,0,0,0);
//	led_switch.publish(led);
//	suspendCallerUntil(NOW()+100*MILLISECONDS);


	// calibrate Gyro
	int16_t temp[3];
	int16_t gyro_temp[3];
	int16_t accl_temp[3];

	for(int i=0;i<CALIBRAION_SAMPLES;i++){
		read_multiple_Register(IMU_GYRO,(X_ANGULAR_L),6,temp);
		gyro_temp[0] += temp[0];
		gyro_temp[1] += temp[1];
		gyro_temp[2] += temp[2];
		//		if(i%100 == 0)PRINTF("\novf;  %d,%d,%d",gyro_temp[0],gyro_temp[1],gyro_temp[2]);
	}
	gyroOffset[0] = (gyro_temp[0] / CALIBRAION_SAMPLES);
	gyroOffset[1] = (gyro_temp[1] / CALIBRAION_SAMPLES);
	gyroOffset[2] = (gyro_temp[2] / CALIBRAION_SAMPLES);
	PRINTF("CAL: %f, %f, %f",gyroOffset[0],gyroOffset[1],gyroOffset[2]);

//	setLEDMask(2,0,1,0,0);
//	led_switch.publish(led);
	//	int current = NOW()*MILLISECONDS;
//	int timeout = 1000; //in msec
	//	while(timeout < ((NOW() * MILLISECONDS)) -current){
//	suspendCallerUntil(100*MILLISECONDS);

	//	}

	// calibrate accelerometer
	/** WRONG! NEED TO CALIBRATE EACH AXIS SEPARATE ************TODO****************/
	for(int i=0;i<CALIBRAION_SAMPLES;i++){
		read_multiple_Register(IMU_ACCMAG,(X_ACCEL_L),6,temp);
		accl_temp[0] += temp[0];
		accl_temp[1] += temp[1];
		accl_temp[2] += temp[2];
		//		if(i%100 == 0)PRINTF("\novf;  %d,%d,%d",gyro_temp[0],gyro_temp[1],gyro_temp[2]);
	}
	acclOffset[0] = (accl_temp[0] / CALIBRAION_SAMPLES);
	acclOffset[1] = (accl_temp[1] / CALIBRAION_SAMPLES);
	acclOffset[2] = (accl_temp[2] / CALIBRAION_SAMPLES);
	PRINTF("CAL: %f, %f, %f",acclOffset[0],acclOffset[1],acclOffset[2]);

//	setLEDMask(2,0,0,0,0);
//	led_switch.publish(led);


}



void IMU::run(){
	PRINTF("run called\n");
//	calibrateSensors();
	while(1){
		suspendCallerUntil(NOW()+IMU_SAMPLERATE*MILLISECONDS);
		this->readIMU_Data();
		this->convertToRPY();
//		read_multiple_Register(IMU_ACCMAG,X_MAGNETIC_L,6,magn_raw);


	}
}




// HELPER FUNCTOINS
void IMU::setLEDMask(int command,int green,int red,int orange,int blue){
	this->led.COMMAND = command;
	this->led.GREEN = green;
	this->led.RED = red;
	this->led.ORANGE = orange;
	this->led.BLUE = blue;
}

