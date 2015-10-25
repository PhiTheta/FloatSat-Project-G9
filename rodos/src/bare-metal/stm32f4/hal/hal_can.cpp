/*
 * hal_can.cpp
 *
 *  Created on: 04.06.2013
 *      Author: Erik
 */

#include "hal/hal_can.h"
#include "rodos.h"
#include "stm32f4xx_gpio.h"
#include "stm32f4xx_can.h"

#include <new>


#ifndef NO_RODOS_NAMESPACE
namespace RODOS {
#endif

#define MAX_FILTERS 20

#define NUM_HW_FILTER_BANKS 28
#define NUM_CAN1_FILTER_BANKS 14
#define NUM_CAN2_FILTER_BANKS (NUM_HW_FILTER_BANKS-NUM_CAN1_FILTER_BANKS)







void CANGlobalInit(){
	static bool init=false;
	if(!init){
		init=true;

		RCC_APB1PeriphClockCmd((RCC_APB1Periph_CAN1 | RCC_APB1Periph_CAN2), ENABLE);
		RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOD | RCC_AHB1Periph_GPIOB, ENABLE);


	}

}




struct CAN_Filter{
	uint32_t canID;
	uint32_t canIDmask;
	bool isExtID;
	bool rtr;
	HAL_CAN* owner;
	HW_HAL_CAN* owner_context;
};



class CAN_Ctrl{
public:

	bool initialized;
	CAN_TypeDef* can;

	CAN_Filter filters[MAX_FILTERS];
	int numFilters;
	CAN_Filter* hwFilterOrder[MAX_FILTERS];
	Fifo<CanTxMsg,16> txFifo;
	volatile bool txFifoEmpty;

	Semaphore CANCtrlProtector;


	CAN_Ctrl(CAN_TypeDef* _can);
	void init(uint32_t baudrate);

	bool putIntoTxMailbox(CanTxMsg& msg);
	bool setupFilters();

	void TxIRQHandler();
	void RxIRQHandler();


};



class HW_HAL_CAN {
	public:

	CAN_Ctrl* ctrl;
	Fifo<CanRxMsg,64> RxFifo;
	volatile bool rxFifoEmpty;





	public:
	HW_HAL_CAN();


};


CAN_Ctrl CANs[2] = { CAN_Ctrl(CAN1),CAN_Ctrl(CAN2)};





CAN_Ctrl::CAN_Ctrl(CAN_TypeDef* _can){
	initialized = false;
	can=_can;
	numFilters=0;
	txFifoEmpty=true;

}

void CAN_Ctrl::init(uint32_t baudrate){
	if(!initialized){
		initialized=true;

		GPIO_InitTypeDef  GPIO_InitStructure;
		CAN_InitTypeDef CAN_InitStructure;

		CANGlobalInit();

		 /* Connect CAN pins to AF9 */


		  /* Configure CAN RX and TX pins */

		  GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF;
		  GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
		  GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
		  GPIO_InitStructure.GPIO_PuPd  = GPIO_PuPd_UP;

		  if(can==CAN1){
			  GPIO_InitStructure.GPIO_Pin = GPIO_Pin_0 | GPIO_Pin_1;
			  GPIO_PinAFConfig(GPIOD, GPIO_PinSource0, GPIO_AF_CAN1);
			  GPIO_PinAFConfig(GPIOD, GPIO_PinSource1, GPIO_AF_CAN1);
			  GPIO_Init(GPIOD, &GPIO_InitStructure);
		  }else{
			  GPIO_InitStructure.GPIO_Pin = GPIO_Pin_5 | GPIO_Pin_13;
			  GPIO_PinAFConfig(GPIOB, GPIO_PinSource5, GPIO_AF_CAN2);
			  GPIO_PinAFConfig(GPIOB, GPIO_PinSource13, GPIO_AF_CAN2);
			  GPIO_Init(GPIOB, &GPIO_InitStructure);
		  }


		  //Prescaler Calculation

		  RCC_ClocksTypeDef RCC_ClocksStatus;
		  RCC_GetClocksFreq(&RCC_ClocksStatus);

		  uint32_t canCtrlFreq =  RCC_ClocksStatus.PCLK1_Frequency;
		  PRINTF("CAN Ctrl Frequency: %lud",canCtrlFreq);






		  /* CAN configuration ********************************************************/
		  /* Enable CAN clock */


		  /* CAN register init */
		  CAN_DeInit(can);

		  /* CAN cell init */
		  CAN_InitStructure.CAN_TTCM = DISABLE;
		  CAN_InitStructure.CAN_ABOM = DISABLE;
		  CAN_InitStructure.CAN_AWUM = DISABLE;
		  CAN_InitStructure.CAN_NART = DISABLE;
		  CAN_InitStructure.CAN_RFLM = DISABLE;
		  CAN_InitStructure.CAN_TXFP = ENABLE;
		  CAN_InitStructure.CAN_Mode = CAN_Mode_Normal;
		  CAN_InitStructure.CAN_SJW = CAN_SJW_1tq;

		  /* CAN Baudrate = 1 MBps (CAN clocked at 30 MHz) */
		/*  CAN_InitStructure.CAN_BS1 = CAN_BS1_6tq; // was CAN_BS1_6tq
		  CAN_InitStructure.CAN_BS2 = CAN_BS2_8tq; //was 7*/
		  CAN_InitStructure.CAN_BS1 = CAN_BS1_8tq; // was CAN_BS1_6tq
		  CAN_InitStructure.CAN_BS2 = CAN_BS2_5tq; //was 7


		  CAN_InitStructure.CAN_Prescaler =  canCtrlFreq/(baudrate*(1+6+7));

		  if(canCtrlFreq % (baudrate*(1+6+7))!=0){
			  ERROR("CAN Baudrate not possible with current settings and CAN Clock!\n");
		  }

		  //CAN_InitStructure.CAN_Prescaler =  6;


		  if(CAN_Init(can, &CAN_InitStructure)==CAN_InitStatus_Failed){
			  ERROR("CAN init failed\n");
		  }


		  CAN_ITConfig(can,CAN_IT_FMP0,ENABLE);


		 CAN_SlaveStartBank(NUM_CAN1_FILTER_BANKS);
		  setupFilters();



		  if(can==CAN1){
			  CANs[1].setupFilters();
			  NVIC_EnableIRQ(CAN1_TX_IRQn);
			  NVIC_EnableIRQ(CAN1_RX0_IRQn);
		  }else{
			  NVIC_EnableIRQ(CAN2_TX_IRQn);
			  NVIC_EnableIRQ(CAN2_RX0_IRQn);
		  }





	}



}

bool CAN_Ctrl::putIntoTxMailbox(CanTxMsg& msg){


	return CAN_Transmit(can,&msg) != CAN_TxStatus_NoMailBox;

}

bool CAN_Ctrl::setupFilters(){

	int bank=0;
	int subBank;
	int filterIndex=0;
	int bankOffset,numBanks;
	bankOffset = (can==CAN1) ? 0 : NUM_CAN1_FILTER_BANKS;
	numBanks   = (can==CAN1) ? NUM_CAN1_FILTER_BANKS : NUM_CAN2_FILTER_BANKS;
	CAN_Filter* f;
	CAN_FilterInitTypeDef filterInit;
	filterInit.CAN_FilterFIFOAssignment= CAN_FIFO0;
	filterInit.CAN_FilterActivation = ENABLE;



#define IS_16BIT ((!f->isExtID) || ((~(f->canIDmask & 0x7FFF))==0))
#define IS_SINGLEID_WHEN32 ( f->canIDmask==0)
#define IS_SINGLEID_WHEN16 ( (f->canIDmask & (0x1FFF << 16))==0)


	//32 Bit Filters with mask
	filterInit.CAN_FilterScale=CAN_FilterScale_32bit;
	filterInit.CAN_FilterMode=CAN_FilterMode_IdMask;
	for(int i=0;i<numFilters;i++){
		f = &filters[i];
		if(!IS_16BIT && !IS_SINGLEID_WHEN32 ){

			if(bank >= numBanks)
				return false;
			filterInit.CAN_FilterNumber=bankOffset+bank;
			bank++;

			filterInit.CAN_FilterIdLow = ((f->canID << 3)& 0xFFFF) | 0b100 ;
			filterInit.CAN_FilterIdHigh= (f->canID >> 13)& 0xFFFF;
			if(f->rtr)
				filterInit.CAN_FilterIdLow |= 0b10;
			filterInit.CAN_FilterMaskIdLow = ~((f->canIDmask << 3)& 0xFFFF);
			filterInit.CAN_FilterMaskIdHigh= ~((f->canIDmask >> 13)& 0xFFFF);

			hwFilterOrder[filterIndex]=f;
			filterIndex++;

			PRINTF("Added 32bit mask filter\n");
			CAN_FilterInit(&filterInit);

		}

	}

	//32 Bit Filters without mask
	filterInit.CAN_FilterScale=CAN_FilterScale_32bit;
	filterInit.CAN_FilterMode=CAN_FilterMode_IdList;
	subBank=0;
	for(int i=0;i<numFilters;i++){
		f = &filters[i];
		if(!IS_16BIT && IS_SINGLEID_WHEN32 ){


			if(subBank==0){
				filterInit.CAN_FilterIdLow = ((f->canID << 3)& 0xFFFF) | 0b100 ;
				filterInit.CAN_FilterIdHigh= (f->canID >> 13)& 0xFFFF;
				if(f->rtr)
					filterInit.CAN_FilterIdLow |= 0b10;

				subBank++;
				hwFilterOrder[filterIndex]=f;
				filterIndex++;

			}else{
				filterInit.CAN_FilterMaskIdLow = ((f->canID << 3)& 0xFFFF) | 0b100 ;
				filterInit.CAN_FilterMaskIdHigh= (f->canID >> 13)& 0xFFFF;
				if(f->rtr)
					filterInit.CAN_FilterMaskIdLow |= 0b10;



				if(bank >= numBanks)
					return false;
				filterInit.CAN_FilterNumber=bankOffset+bank;
				bank++;

				hwFilterOrder[filterIndex]=f;
				filterIndex++;

				CAN_FilterInit(&filterInit);

				subBank=0;
			}
			PRINTF("Added 32bit non-mask filter\n");




		}

	}
	if(subBank==1){

		filterInit.CAN_FilterMaskIdLow = filterInit.CAN_FilterIdLow;
		filterInit.CAN_FilterMaskIdHigh= filterInit.CAN_FilterIdHigh;

		hwFilterOrder[filterIndex]=hwFilterOrder[filterIndex-1];
		filterIndex++;
		if(bank >= numBanks)
			return false;
		filterInit.CAN_FilterNumber=bankOffset+bank;
		bank++;

		CAN_FilterInit(&filterInit);
	}




	//16 Bit Filters with mask
	filterInit.CAN_FilterScale=CAN_FilterScale_16bit;
	filterInit.CAN_FilterMode=CAN_FilterMode_IdMask;
	subBank=0;
	for(int i=0;i<numFilters;i++){
		f = &filters[i];
			if(IS_16BIT && !IS_SINGLEID_WHEN16 ){


				if(subBank==0){
					filterInit.CAN_FilterIdLow = ((f->canID    >> 13)& 0xFFE0) | ((f->canID      >> 15)& 0b111 );
					filterInit.CAN_FilterIdHigh= ~( ((f->canIDmask >> 13)& 0xFFE0) | ((f->canIDmask  >> 15)& 0b111 ) );
					if(f->isExtID)
						filterInit.CAN_FilterIdLow |= 0b1000;
					if(f->rtr)
						filterInit.CAN_FilterIdLow |= 0b10000;

					subBank++;
					hwFilterOrder[filterIndex]=f;
					filterIndex++;

				}else{
					filterInit.CAN_FilterMaskIdLow = ((f->canID    >> 13)& 0xFFE0) | ((f->canID      >> 15)& 0b111 );
					filterInit.CAN_FilterMaskIdHigh= ~( ((f->canIDmask >> 13)& 0xFFE0) | ((f->canIDmask  >> 15)& 0b111 ) );
					if(f->isExtID)
						filterInit.CAN_FilterMaskIdLow |= 0b1000;
					if(f->rtr)
						filterInit.CAN_FilterMaskIdLow |= 0b10000;

					hwFilterOrder[filterIndex]=f;
					filterIndex++;

					if(bank >= numBanks)
						return false;
					filterInit.CAN_FilterNumber=bankOffset+bank;
					bank++;

					CAN_FilterInit(&filterInit);

					subBank=0;
				}


				PRINTF("Added 16bit mask filter\n");

			}

	}
	if(subBank==1){

		filterInit.CAN_FilterMaskIdLow = filterInit.CAN_FilterIdLow;
		filterInit.CAN_FilterMaskIdHigh= filterInit.CAN_FilterIdHigh;

		hwFilterOrder[filterIndex]=hwFilterOrder[filterIndex-1];
		filterIndex++;
		if(bank >= numBanks)
			return false;
		filterInit.CAN_FilterNumber=bankOffset+bank;
		bank++;

		CAN_FilterInit(&filterInit);
	}


	//16 Bit Filters without mask
	filterInit.CAN_FilterScale=CAN_FilterScale_16bit;
	filterInit.CAN_FilterMode=CAN_FilterMode_IdList;
	subBank=0;
	for(int i=0;i<numFilters;i++){
		f = &filters[i];
		if(IS_16BIT && IS_SINGLEID_WHEN16 ){

			uint16_t* subFilter;
			switch(subBank){
			case 0:
				subFilter= &filterInit.CAN_FilterIdLow;
				break;
			case 1:
				subFilter= &filterInit.CAN_FilterIdHigh;
				break;

			case 2:
				subFilter= &filterInit.CAN_FilterMaskIdLow;
				break;

			case 3:
				subFilter= &filterInit.CAN_FilterMaskIdHigh;
				break;



			}
			*subFilter = ((f->canID    >> 13)& 0xFFE0) | ((f->canID      >> 15)& 0b111 );
			if(f->isExtID)
				*subFilter |= 0b1000;
			if(f->rtr)
				*subFilter |= 0b10000;

			hwFilterOrder[filterIndex]=f;
			filterIndex++;

			subBank++;

			if(subBank ==4){
				if(bank >= numBanks)
					return false;
				filterInit.CAN_FilterNumber=bankOffset+bank;
				bank++;

				CAN_FilterInit(&filterInit);

				subBank=0;


			}

			PRINTF("Added 16bit non-mask filter\n");
		}

	}
	if(subBank >0){
		while(subBank <4){
			switch(subBank){

			case 1:
				filterInit.CAN_FilterIdHigh=filterInit.CAN_FilterIdLow;
				break;

			case 2:
				filterInit.CAN_FilterMaskIdLow=filterInit.CAN_FilterIdHigh;
				break;

			case 3:
				filterInit.CAN_FilterMaskIdHigh=filterInit.CAN_FilterMaskIdLow;
				break;

			}

			hwFilterOrder[filterIndex]=hwFilterOrder[filterIndex-1];
			filterIndex++;
			subBank++;
		}

		if(bank >= numBanks)
			return false;
		filterInit.CAN_FilterNumber=bankOffset+bank;
		bank++;

		CAN_FilterInit(&filterInit);
	}

	return true;
}



void CAN_Ctrl::TxIRQHandler(){
	bool sending=true;

	while(sending){
		sending=false;
		for(int mbox=0; mbox<3;mbox++){
			if(CAN_TransmitStatus(can,mbox)!=CAN_TxStatus_Pending){
				CanTxMsg msg;

				if(!txFifo.get(msg)){
					sending=false;
					txFifoEmpty=true;
					CAN_ITConfig(can,CAN_IT_TME,DISABLE);
					//This may call upCall multiple times for one hal, but it is simpleset solution
					for (int i=0;i<numFilters;i++){
						filters[i].owner->upCallWriteFinished();
					}
					break;
				}

				putIntoTxMailbox(msg);
				sending=true;
			}

		}

	}

	CAN_ClearFlag(can,CAN_FLAG_RQCP0 | CAN_FLAG_RQCP1 | CAN_FLAG_RQCP2);
	CAN_ClearITPendingBit(can,CAN_IT_TME);
}


void CAN_Ctrl::RxIRQHandler(){
	CanRxMsg msg;



	while(CAN_MessagePending(can,CAN_FIFO0)>0){
		CAN_Receive(can,CAN_FIFO0,&msg);

		HW_HAL_CAN* target = hwFilterOrder[msg.FMI]->owner_context;

		target->rxFifoEmpty=false;

		if(!target->RxFifo.put(msg)){
			hwFilterOrder[msg.FMI]->owner->upCallDataReady();
			//xprintf("CAN input FIFO overflow\n");
			break; // Drop Message and Abort receving when Software FIFO is full
		}
		hwFilterOrder[msg.FMI]->owner->upCallDataReady();

	}

}




HW_HAL_CAN::HW_HAL_CAN(){

}




HAL_CAN::HAL_CAN(CAN_IDX canIdx){
	context= new(xmalloc(sizeof(HW_HAL_CAN))) HW_HAL_CAN();
	context->ctrl=&CANs[canIdx];
	context->rxFifoEmpty=true;

}

int HAL_CAN::init(unsigned int baudrate){
	context->ctrl->init(baudrate);
	return 0;


}

void HAL_CAN::reset(){
	context->ctrl->CANCtrlProtector.enter();
	CAN_DeInit(context->ctrl->can);
	context->ctrl->initialized=false;
	context->ctrl->CANCtrlProtector.leave();
}

int HAL_CAN::config(CAN_PARAMETER_TYPE type, int paramVal){
	switch(type){
	case CAN_PARAMETER_BAUDRATE:
		context->ctrl->CANCtrlProtector.enter();
			reset();
			init(paramVal);
		context->ctrl->CANCtrlProtector.leave();
	return 0;


	}
	return -1;
}


int  HAL_CAN::status(CAN_STATUS_TYPE type){
	int tmp;
	switch(type){
	case CAN_STATUS_RX_ERROR:
		return CAN_GetReceiveErrorCounter(context->ctrl->can);
	case CAN_STATUS_TX_ERROR:
		tmp = CAN_GetLSBTransmitErrorCounter(context->ctrl->can);
		if(CAN_GetFlagStatus(context->ctrl->can,CAN_FLAG_BOF)==SET){
			return tmp + +256;
		}else{
			return tmp;
		}
	case CAN_STATUS_RX_LEVEL:
		return -1;
	}
	return -1;

}

bool  HAL_CAN::isWriteFinished(){
    return context->ctrl->txFifo.isEmpty();
}


bool  HAL_CAN::isDataReady(){
	//return !context->rxFifoEmpty;
	return !context->RxFifo.isEmpty();
}

bool  HAL_CAN::addIncomingFilter(uint32_t ID, uint32_t IDMask, bool extID, bool rtr){

	if(context->ctrl->numFilters >= MAX_FILTERS)
		return false;

	context->ctrl->CANCtrlProtector.enter();

	context->ctrl->filters[context->ctrl->numFilters].canID=     (extID ? ID     : ID     << 18) & 0x1FFFFFFF;
	context->ctrl->filters[context->ctrl->numFilters].canIDmask= (extID ? IDMask : IDMask << 18) & 0x1FFFFFFF;
	context->ctrl->filters[context->ctrl->numFilters].isExtID=extID;
	context->ctrl->filters[context->ctrl->numFilters].rtr=rtr;
	context->ctrl->filters[context->ctrl->numFilters].owner=this;
	context->ctrl->filters[context->ctrl->numFilters].owner_context=this->context;

	context->ctrl->numFilters++;


	bool result;
	result = context->ctrl->setupFilters();

	context->ctrl->CANCtrlProtector.leave();
	return result;


}




int HAL_CAN::write(const char* sendBuf, int len,uint32_t canID,  bool extID, bool rtr){


	CanTxMsg msg;

	if(len > 8) return -1;

	if(extID){
		msg.ExtId=canID;
		msg.IDE=CAN_ID_EXT;
	}else{
		msg.StdId=canID;
		msg.IDE=CAN_ID_STD;
	}
	msg.RTR = rtr ? CAN_RTR_Remote : CAN_RTR_DATA;
	msg.DLC=len;
	memcpy(msg.Data,sendBuf,len);

	//PRINTF("len=%d data:%d ",len,*(int*)msg.Data);

	context->ctrl->CANCtrlProtector.enter();

	CAN_ITConfig(context->ctrl->can,CAN_IT_TME,DISABLE);

	if(context->ctrl->txFifoEmpty){

		if(context->ctrl->putIntoTxMailbox(msg)){
			// Software FIFO is empty, no need for TX interrupts,leaving it disabled
			context->ctrl->CANCtrlProtector.leave();
			return 0;
		}
	}

	context->ctrl->txFifoEmpty=false;
	CAN_ITConfig(context->ctrl->can,CAN_IT_TME,ENABLE);
	if (!context->ctrl->txFifo.put(msg)){
		context->ctrl->CANCtrlProtector.leave();
		return -1;
	}

	context->ctrl->txFifoEmpty=false;
	CAN_ITConfig(context->ctrl->can,CAN_IT_TME,ENABLE);
	context->ctrl->CANCtrlProtector.leave();


	return 0;
}



int HAL_CAN::read(char* recBuf, uint32_t* canID, bool* isExtID,bool* rtr){
	CanRxMsg msg;

	if(context->RxFifo.get(msg)){

		if(msg.IDE){
			if(canID){
				*canID=msg.ExtId;
			}
			if(isExtID){
				*isExtID=true;
			}
		}else{
			if(canID){
				*canID=msg.StdId;
			}
			if(isExtID){
				*isExtID=false;
			}
		}
		if(rtr){
			*rtr= (msg.RTR==CAN_RTR_Remote);
		}


		for(int i=0;i<8;i++){
			recBuf[i]=msg.Data[i];
		}


		return msg.DLC;

	}

	context->rxFifoEmpty=true;
	return -1;

}

extern "C"{


	void CAN1_TX_IRQHandler(){
		CANs[0].TxIRQHandler();
		NVIC_ClearPendingIRQ(CAN1_TX_IRQn);
	}


	void CAN2_TX_IRQHandler(){
		CANs[1].TxIRQHandler();
		NVIC_ClearPendingIRQ(CAN2_TX_IRQn);
	}

	void CAN1_RX0_IRQHandler(){
		CANs[0].RxIRQHandler();
		NVIC_ClearPendingIRQ(CAN1_RX0_IRQn);
	}


	void CAN2_RX0_IRQHandler(){
		CANs[1].RxIRQHandler();
		NVIC_ClearPendingIRQ(CAN2_RX0_IRQn);
	}

}


#ifndef NO_RODOS_NAMESPACE
}
#endif