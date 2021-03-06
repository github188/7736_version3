#include "device.h"
#include "global.h"
#include "common.h"
#include "CommonFunction.h"
#include "key.h"
#include "STRING.H"
#include "DataExChange.h"
#include "mainflow.h"
#include "timer.h"
#include "SstFlash.h"
#include "procotol.h"
#include "casher.h"
#include "IOInput.h"
#include "communication.h"

#include "Serial1.h"
#include "scheduler.h"

#include "stdio.h"
#include "stdlib.h"
#include "string.h"
#include "stdarg.h"



//VMC通知GCC复位初始化
#define VMC_RESET_REQ				(0x80)
//VMC通知GCC报告GCC状态
#define VMC_STATUS_REQ				(0x81)
//VMC通知GCC出货
#define VMC_VENDING_REQ				(0x82)
//VMC通知GCC报告出货情况
#define VMC_VENDINGRESULT_REQ		(0x83)


//GCC通知VMC复位初始化
#define GCC_RESET_ACK				(0x00)
//GCC通知VMC报告GCC状态
#define GCC_STATUS_ACK				(0x01)
//GCC通知VMC出货
#define GCC_VENDING_ACK				(0x02)
//GCC通知VMC报告出货情况
#define GCC_VENDINGRESULT_ACK		(0x03)
//GCC通知VMC开门
#define GCC_OPENDOOR_ACK			(0x04)
//GCC通知VMC关门
#define GCC_CLOSEDOOR_ACK			(0x05)
//GCC通知VMC打开照明灯
#define GCC_OPENLIGHT_ACK			(0x06)
//GCC通知VMC关闭照明灯
#define GCC_CLOSELIGHT_ACK			(0x07)
//GCC通知VMC报告配置参数
#define GCC_GETINFO_ACK				(0x08)




//出货操作
#define CHANNEL_OUTGOODS		(1)
//检测货道当前状态			
#define CHANNEL_CHECKSTATE		(2)
//检测货道上次的出货结果			
#define CHANNEL_CHECKOUTRESULT	(3)	
//清楚sn
#define CHANNEL_CLEARSN			(4)



static struct COMMTASK xdata lifterTask,ackTask;
uchar lifterSendMsg(unsigned char type,uchar binNo,uchar rowNo,uchar columNo);
uchar LifterVendoutTask(struct COMMTASK xdata*liftPtr);
uint LifterCheckStateTask(uchar binNo,uchar *errNo);
uchar LifterVendout(uchar binNum,uchar logicNo);













	


void lifterDelay(unsigned long timeout)
{
	lifterTimer = timeout / 10;
	while(lifterTimer);
}



/*****************************************************************************
** Function name:	ZhkLifterRxMsg	
** Descriptions:	      接收串口回应包													 			
** parameters:		无
** Returned value:	0:无数据接收 1:接收完毕 2:数据接收错误
*****************************************************************************/
u_char ZhkLifterRxMsg( void )
{
	
	uchar i,index = 0,rcx = 50;
	uint crc;
	memcpy( &ackTask, &CurrentTask, sizeof( struct COMMTASK ) );
	while(rcx)
	{
		if(ZhkSerial1IsRxBufNull())//无数据 需要延时等待一下
		{
			for(i = 0;i < 10;i++)
			{
				i = i;
			}
			rcx--;
		}
		else
		{
			ackTask.Param[index] = ZhkSerial1GetCh();
			if(index == 0)
			{
				//判断包头
				if(ackTask.Param[0] != 0xC8)//head err
					continue;
			}
			else if(index == 1)
				ackTask.ParamLen = ackTask.Param[index];
			index++;
			if(index >= 7)
			{
				crc = CrcCheck(ackTask.Param,ackTask.ParamLen);
				return 1;
#if 0
				if(ackTask.Param[index - 2] == crc / 256 && ackTask.Param[index-1] == crc % 256)
					return 1;
				else
					return 2;
#endif
			}
			
		}
	}
	
	if(index)
		return 2;
	else
		return 0;
	
	
}




/*********************************************************************************************************
** Function name:     	ZhkLifterTxMsg
** Descriptions:	       升降机发送串口
** input parameters:    
** output parameters:   无
** Returned value:      CRC检验结果
*********************************************************************************************************/
bit ZhkLifterTxMsg( struct COMMTASK xdata* NewTask )
{
	uchar i,len;
	uint  crc;
	len = NewTask->Param[1];
	crc	= CrcCheck(NewTask->Param,len);//加校验
	NewTask->Param[len] = (uchar)(crc >> 8);
	NewTask->Param[len + 1] = (uchar)(crc & 0xFF);
	for(i = 0;i < len + 2;i++)
	{	
		ZhkSerial1PutCh( NewTask->Param[i]);
	}
	
	return 1;	
}

//////////////////////////////////////////////////////////
///货道处理1表示
//////////////////////////////////////////////////////////
bit  ZhkLifterTask( struct COMMTASK xdata* TaskTemp )
{
	switch( ZhkSchedulerState )
	{
	case TASK_NULL:
		break;
	case TASK_REDAY:	//发送串口请求
		ZhkChannelSwitch( EQUIP_CHANNEL );	
		if(TaskTemp->Cmd == VMC_VENDING_REQ)//出货走另一个命令
		{
			Channel.State = LifterVendoutTask(TaskTemp);
			ZhkSchedulerState = TASK_FINISH;
		}
		else
		{
			ZhkLifterTxMsg( TaskTemp );
			ZhkSchedulerState = TASK_WAIT;
			ZhkDownMsgAckTimer  = 500;//15秒 等待
			Channel.CommState = COMM_BUZY;
			Channel.ExtState[2] = 0;
		}
		
		break;	
	case TASK_WAIT:
		if(!ZhkSerial1IsRxBufNull())//有数据
		{
			ZhkSchedulerState = TASK_FINISH;
			Channel.State = ZhkLifterRxMsg();
			break;
		}
		
		if ( ZhkDownMsgAckTimer == 0 )//超时
		{
			ZhkSchedulerState = TASK_ERROR;					
		}		
		break;			
	case TASK_FINISH:
		ZhkSchedulerState = TASK_NULL; 		
		Channel.CommState   = COMM_COMMOK;
		memset( &CurrentTask, 0, sizeof( struct COMMTASK ) );
		ZhkDownMsgAckTimer = 0;
		break;
	case TASK_ERROR:
		Channel.CommState = COMM_TIMEOUT;
		ZhkSchedulerState   = TASK_NULL; 
		memset( &CurrentTask, 0, sizeof( struct COMMTASK ) );
		break;
	default:
		ZhkSchedulerState = TASK_NULL;
		
	}
	return 1;
}

/*****************************************************************************
** Function name:	LifterSendVendout	
** Descriptions:	      	发送出货查询结果命令													 			
** parameters:		binNo:柜号
** Returned value:	低八位  0:查询失败 1:出货完成 2:正在出货
								0xFF:通信故障 
					

					高八位 仅"出货失败 以下字段有效"
					0:出货成功
					1:数据错误
					2:无货				3:卡货
					4:取货门没开启
					5:货物未取走 		6:货柜大门被打开升
					7:降机构故障  	other:其他故障
*****************************************************************************/
uint LifterSendCheckRst(uchar binNo)
{
	uchar rst = 0;
	rst = lifterSendMsg(VMC_VENDINGRESULT_REQ,binNo,0,0);
	if(rst == 0)
		return 0xFF;
	if(ackTask.Param[4] == GCC_VENDINGRESULT_ACK)//成功收到ACK
	{	
		if(ackTask.Param[5] == 0x01)//正在出货
			return 2;
		else
		{
			if(ackTask.Param[6] == 0x00)//出货成功
				return 1;
			else
				return (((uint)ackTask.Param[7] << 8)| (0x01));
		}
	}
	return 0;
	
}



/*****************************************************************************
** Function name:	LifterSendCheckState	
** Descriptions:	      	发送查询状态命令													 			
** parameters:		binNo:柜号
** Returned value:	低八位0整机空闲 1整机忙2查询失败0xFF通信失败
					高八位(按位算置1表示有故障)
					bit0 整机状态故障
					bit1升降机故障
					bit2取货电机故障
					bit3用户电动门故障
					bit4弹币机构故障
					bit5防盗模块故障
					bit6货到控制器故障
					bit7货道大门故障
*****************************************************************************/
uint LifterSendCheckState(uchar binNo)
{
	uchar rst = 0,state,errNo = 0,rcx = 2;
	while(rcx--)
	{
		rst = lifterSendMsg(VMC_STATUS_REQ,binNo,0,0);
		if(rst == 0)
			continue;		
		if(ackTask.Param[4] == GCC_STATUS_ACK)//成功收到ACK
		{	
			state = (ackTask.Param[5] == 0x01) ? 1 : 0;//整机标志 0空闲 1忙
			errNo |= (ackTask.Param[6] & 0x01) << 0;//整机状态
			errNo |= (ackTask.Param[7] & 0x01) << 1;//升降机
			errNo |= (ackTask.Param[8] & 0x01) << 2;//取货电机
			errNo |= (ackTask.Param[9] & 0x01) << 3;//用户电动门
			errNo |= (ackTask.Param[10] & 0x01) << 4;//弹币机构
			errNo |= (ackTask.Param[11] & 0x01) << 5;//防盗模块
			errNo |= (ackTask.Param[12] & 0x01) << 6;//货到控制器
			errNo |= (ackTask.Param[13] & 0x01) << 6;//货道大门	
			
			return (((uint)errNo << 8) | state);
			
		}
		return 2;
	}

	return 0xFF;
	
}



/*****************************************************************************
** Function name:	LifterSendVendout	
** Descriptions:	      	发送出货命令													 			
** parameters:		binNo:柜号
					rowNo:层号
					columnNo:道号
** Returned value:	0:发送失败; 1:发送成功;0xFF 通信失败
*****************************************************************************/
uchar LifterSendVendout(uchar binNo,uchar rowNo,uchar columnNo)
{
	uchar rst = 0,rcx = 2;
	while(rcx--)
	{
		rst = lifterSendMsg(VMC_VENDING_REQ,binNo,rowNo,columnNo);
		if(rst == 0)
			return 0xFF;
		if(ackTask.Param[4] == GCC_VENDING_ACK)//成功收到ACK
		{
			return 1;
		}
		//如果没有收到ACK则需要在发送一次查询命令
		//确保出货命令发送成功避免重复发送出货命令
		if(LifterSendCheckRst(binNo) == 2)//表示机器繁忙 同样意味着出货命令发送成功
			return 1;
	}
	return 0;
}


/*****************************************************************************
** Function name:	lifterSendMsg	
** Descriptions:													 			
** parameters:		type:操作类型
					binNo:箱柜编号
					rowNo:层编号
					columNo:货道号					
** Returned value:	0:通信失败; 1:通信成功;
*****************************************************************************/
uchar lifterSendMsg(unsigned char type,uchar binNo,uchar rowNo,uchar columNo)
{
	uchar timeout = 0,comOK = 0,index = 0;
	uchar devType = 0x40;//普通弹簧型 
	binNo = binNo;
	memset( &lifterTask, 0, sizeof( struct COMMTASK ) );
	Channel.CommState   = COMM_BUZY;
	lifterTask.Id 		= ID_CHANNEL;
	lifterTask.Cmd 		= type;
	lifterTask.Priority = PRIORITY_NORMAL; 

	lifterTask.Param[index++] = 0xC7;
	lifterTask.Param[index++] = 0x05;//长度
	lifterTask.Param[index++] = devType;
	lifterTask.Param[index++] = 0x00;//版本固定0
	lifterTask.Param[index++] = type;
	if(rowNo != 0x00)
		lifterTask.Param[index++] = rowNo;
	if(columNo != 0x00)
		lifterTask.Param[index++] = columNo;

	lifterTask.ParamLen	= index;
	lifterTask.Param[1] = index;//长度重定位

	ZhkSchedulerAddTask( &lifterTask );
	while(! ( timeout || comOK ))
	{
		WaitForWork( 50, NULL );
		timeout = TestDeviceTimeOut( &Channel );
		comOK = TestDeviceCommOK( &Channel );	
	}

	if(timeout)
		return 0;
	if(comOK)
		return (Channel.State == 1);	
	return 0;


}



/*****************************************************************************
** Function name:	LifTerProcess	
** Descriptions:														 			
** parameters:		无				
** Returned value:	01:成功
					11:数据错误 
					12:无货
					13:卡货		  
					14:取货门没开启
					15:货物未取走
					16:货柜大门被打开
					17:升降机构故障
					21:整机故障
					22:升降机故障
					23:取货电机故障
					24:用户电动门故障
					25:弹币机构故障
					26:防盗模块故障
					27:货到控制器故障
					28:货道大门故�
					51:状态查询超时
					52:出货命令发送失败
					53:出货结果查询超时
					99:其他故障
					255(0xFF):通信失败
*******************************************************************************/
uchar LifTerProcess(uchar binNum,uchar logicNo)
{
	uchar Result[36]={0},res = 0,rcx = 5;
	uchar rowNo = 0,columnNo = 0,stateH,stateL;
	unsigned short takeTimeOut = 10,vendoutTimeout;
	uint liftState;
	if(logicNo == 0x00)
		return 0xff;
	rowNo = logicNo / 10 ;
	columnNo = logicNo % 10;
	
	//开始出货 //查询状态是否正常，正常出货
	rcx = 10;
	while(rcx--)
	{
		liftState = LifterSendCheckState(binNum); 
		stateL = liftState & 0xFF;
		stateH = (liftState >> 8) & 0xFF;
		if(stateL == 0)//正常就跳开
			break;
		else if(stateL == 0xFF)//通信失败
		{
			return 0xFF;
		}
		else 
		{
			if(stateH & 0xFF)//有故障
			{
				if(stateH & 0x01) return 21;
				if(stateH & 0x02) return 22;
				if(stateH & 0x04) return 23;
				if(stateH & 0x08) return 24;
				if(stateH & 0x10) return 25;
				if(stateH & 0x20) return 26;
				if(stateH & 0x40) return 27;
				if(stateH & 0x80) return 28;
				return 99;
					
			}
		}
		WaitForWork( 1000, NULL );
	}

	if(liftState != 0)//超时查询任然不正常
		return 51;
	
	while(1)
	{
		//进行出货操作
		res = LifterSendVendout(binNum,rowNo,columnNo);
		if(res == 0 || res == 0xFF)
			return 52;
		
		WaitForWork(50, NULL );
		//5000 * 10  = 50000 
		vendoutTimeout = 100 +(8 - rowNo) * 10;//检测出货结果超时
		while(vendoutTimeout--)
		{	
			liftState = LifterSendCheckRst(binNum);//检测出货结果
			if((liftState & 0xFF) == 0x01)//出货完成
			{
				break;
			}
			WaitForWork( 1000, NULL );
		}
		
		if(vendoutTimeout)
		{
			if((liftState & 0xFF00))//出货失败
			{
				if((((liftState >> 8) & 0xFF) == 5) && takeTimeOut)//货未取走
				{
					WaitForWork( 2000, NULL );
					takeTimeOut--;
					continue;
				}
				stateH = (liftState >> 8) & 0xFF ;
				return ((stateH > 7) ? (stateH + 10) : 99);
			}	
			else//出货成功
				return 1;
		}
		else										   	
			return 53;		
	}
//	return 0xFF;
}



uchar LifterVendoutReturn(uchar res,uchar flag)
{
	uchar val = 0;
	switch(res)
	{
		case 1:
#ifdef _CHINA_
			if(flag == 1)
			DisplayStr( 0, 1, 1, "出货成功", sizeof("出货成功") );
#endif
			val = 0;
			break;
		case 11:
#ifdef _CHINA_
			if(flag == 1)
			DisplayStr( 0, 1, 1, "数据错误", sizeof("数据错误") );
#endif
			//DisplayStr( 0, 1, 1, "Err data", sizeof("Err data") );
			
			val = 11;
			break;
		case 12:
#ifdef _CHINA_	
			if(flag == 1)
			DisplayStr( 0, 1, 1, "货道无货", sizeof("货道无货") );
			//DisplayStr( 0, 1, 1, "No goods", sizeof("No goods") );
#endif
			val = 3;
			break;
		case 13:
			#ifdef _CHINA_
			if(flag == 1)
			DisplayStr( 0, 1, 1, "货道卡货", sizeof("货道卡货") );
			//DisplayStr( 0, 1, 1, "Ka Huo", sizeof("Ka Huo") );
			#endif
			val = 13;
			break;
		case 14:
			#ifdef _CHINA_
			if(flag == 1)
			DisplayStr( 0, 1, 1, "取货门没开启", sizeof("取货门没开启") );
			//DisplayStr( 0, 1, 1, "Ka Huo", sizeof("Ka Huo") );
			#endif
			val = 14;
			break;
		case 15:
			#ifdef _CHINA_
			if(flag == 1)
			DisplayStr( 0, 1, 1, "货物未取走", sizeof("货物未取走") );
			//DisplayStr( 0, 1, 1, "Mei Qu zou", sizeof("Mei Qu zou") );
			#endif
			val = 15;
			break;
		case 16:
			#ifdef _CHINA_
			if(flag == 1)
			DisplayStr( 0, 1, 1, "货柜大门被打开", sizeof("货柜大门被打开") );
			#endif
			val = 16;
			break;
		case 17:
			#ifdef _CHINA_
			if(flag == 1)
			DisplayStr( 0, 1, 1, "升降机构故障", sizeof("升降机构故障") );
			//DisplayStr( 0, 1, 1, "Err Lift", sizeof("Err Lift") );
			#endif
			val = 17;
			break;
		case 21:
			#ifdef _CHINA_
			if(flag == 1)
			DisplayStr( 0, 1, 1, "整机故障", sizeof("整机故障") );
			#endif
			val = 21;
			break;
		case 22:
			#ifdef _CHINA_
			if(flag == 1)
			DisplayStr( 0, 1, 1, "升降机故障", sizeof("升降机故障") );
			#endif
			val = 22;
			break;
		case 23:
			#ifdef _CHINA_
			if(flag == 1)
			DisplayStr( 0, 1, 1, "取货电机故障", sizeof("取货电机故障") );
			#endif
			val = 23;
			break;
		case 24:
			#ifdef _CHINA_
			if(flag == 1)
			DisplayStr( 0, 1, 1, "用户电动门故障", sizeof("用户电动门故障") );
			#endif
			val = 24;
			break;
		case 25:
			#ifdef _CHINA_
			if(flag == 1)
			DisplayStr( 0, 1, 1, "弹币机构故障", sizeof("弹币机构故障") );
			#endif
			val = 25;
			break;
		case 26:
			#ifdef _CHINA_
			if(flag == 1)
			DisplayStr( 0, 1, 1, "防盗模块故障", sizeof("防盗模块故障") );
			#endif
			val = 26;
			break;
		case 27:
			#ifdef _CHINA_
			if(flag == 1)
			DisplayStr( 0, 1, 1, "货到控制器故障", sizeof("货到控制器故障") );
			#endif
			val = 27;
			break;
		case 28:
			#ifdef _CHINA_
			if(flag == 1)
			DisplayStr( 0, 1, 1, "货道大门故障", sizeof("货道大门故障") );
			#endif
			val = 28;
			break;
		case 51:
			#ifdef _CHINA_
			if(flag == 1)
			DisplayStr( 0, 1, 1, "状态查询超时", sizeof("状态查询超时") );
			#endif
			//DisplayStr( 0, 1, 1, "Err State Check", sizeof("Err State Check") );
			val = 2;
			break;
		case 52:
			#ifdef _CHINA_
			if(flag == 1)
			DisplayStr( 0, 1, 1, "出货命令发送失败", sizeof("出货命令发送失败") );
			#endif
			val = 2;
			//DisplayStr( 0, 1, 1, "Err Send Vendout", sizeof("Err Send Vendout") );
			break;
		case 53:
			#ifdef _CHINA_
			if(flag == 1)
			DisplayStr( 0, 1, 1, "出货结果查询超时", sizeof("出货结果查询超时") );
			#endif
			//DisplayStr( 0, 1, 1, "Err Vendout Check", sizeof("Err Vendout Check") );
			val = 1;
			break;
		case 99:
#ifdef _CHINA_
			if(flag == 1)
			DisplayStr( 0, 1, 1, "其他故障", sizeof("其他故障") );
#endif
			///DisplayStr( 0, 1, 1, "Err Other", sizeof("Err Other") );
			val = 99;
			break;
		case 0xFF:
#ifdef _CHINA_
			if(flag == 1)
			DisplayStr( 0, 1, 1, "通信故障", sizeof("通信故障") );
#endif
			//DisplayStr( 0, 1, 1, "Err Com", sizeof("Err Com") );
			val = 0xFF;
			break;
		default:
#ifdef _CHINA_
			if(flag == 1)
			DisplayStr( 0, 1, 1, "其他故障", sizeof("其他故障") );
#endif
			val = 99;
			break;
	}

	WaitForWork( 2000, NULL );
	return val;
}


/*****************************************************************************
** Function name:	ChannelLifterTask	
** Descriptions:		升降机+传送带出货函数												 			
** parameters:		无				
** Returned value:	0:正确执行的命令1:出货返回失败
**					2:执行命令超时     3:货道无货
**					4:货道故障   		  5:在限定的时间内电机不能到位

**					 6:						7: CMD ERR  8: GOC check error 
*******************************************************************************/
u_char ChannelLifterTask( u_char ChannelNum, u_char TaskId )
{
	u_char xdata res = 0;
	uint temp = 0;
	switch(TaskId)
	{
		case CHANNEL_EXEC:
		case CHANNEL_TEST:
			//res = LifTerProcess(1,ChannelNum);
			res = LifterVendout(1,ChannelNum);
			res = LifterVendoutReturn(res,TaskId == CHANNEL_TEST);
			break;
		case CHANNEL_CLEAR: 
		case CHANNEL_QUERY:
			temp = LifterSendCheckState(1);
			if((temp & 0x00FF) == 2)
				res = 2;
			else
				res = 0;
			break;
		default:
			res = 7;
			break;
	}		
	return res;

	
}



/*****************************************************************************
**任务处理出货动作
*******************************************************************************/

/*****************************************************************************
** Function name:	LifterRecv	
** Descriptions:	      接收串口回应包													 			
** parameters:		无
** Returned value:	0:无数据接收 1:接收完毕 2:数据接收错误
*****************************************************************************/
uchar LifterRecv(uint timeout)
{
	uchar index = 0;
	uint crc;
	timeout = timeout;
	memset( &ackTask, 0, sizeof( struct COMMTASK ) );
	ZhkDownMsgAckTimer = 1500;
	while(ZhkDownMsgAckTimer)
	{
		if(!ZhkSerial1IsRxBufNull())
		{
			ackTask.Param[index++] = ZhkSerial1GetCh();
			if(index == 1)//first byte 
			{
				if(ackTask.Param[0] != 0xC8)//head err
					continue;
			}
			else if(index == 2) //2th byte
				ackTask.ParamLen = ackTask.Param[1];
			else if(index >= (ackTask.ParamLen + 2)) //recv over
			{
				crc = CrcCheck(ackTask.Param,ackTask.ParamLen);
				return 1;
#if 0
				if(ackTask.Param[index - 2] == crc / 256 && ackTask.Param[index-1] == crc % 256)
					return 1;
				else
					return 2;
#endif
			}
			
		}
	}
	
	if(index)
		return 2;
	else
		return 0;
}

/*****************************************************************************
** Function name:	LifterSend	
** Descriptions:	      接收发送并接受													 			
** parameters:		无
** Returned value:	0:无数据接收 1:接收完毕 2:数据接收错误
*****************************************************************************/

uchar LifterSend(unsigned char type,uchar binNo,uchar rowNo,uchar columNo)
{
	uchar timeout = 0,comOK = 0,index = 0,i,buf[12] = {0};
	uint crc;
	binNo = binNo;
	Channel.CommState	= COMM_BUZY;
	buf[index++] = 0xC7;
	buf[index++] = 0x05;//长度
	buf[index++] = 0x40;
	buf[index++] = 0x00;//版本固定0
	buf[index++] = type;
	if(rowNo != 0x00)
		buf[index++] = rowNo;
	if(columNo != 0x00)
		buf[index++] = columNo;
	
	buf[1] = index;//长度重定位
	crc	= CrcCheck(buf,index);//加校验
	buf[index++] = (uchar)(crc >> 8);
	buf[index++] = (uchar)(crc & 0xFF);
	for(i = 0;i < index;i++)
	{	
		ZhkSerial1PutCh( buf[i]);
	}
	
	return LifterRecv(500);


}
	

/*****************************************************************************
** Function name:	LifterCheckRstTask	
** Descriptions:	      	发送出货查询结果命令													 			
** parameters:		binNo:柜号
** Outputparameters: errNo   仅出货完成后一下字段有效
					0:出货成功
					1:数据错误
					2:无货				3:卡货
					4:取货门没开启
					5:货物未取走 		6:货柜大门被打开升
					7:降机构故障  	other:其他故障
** Returned value:	 0:查询失败 1:出货完成 
					 2:正在出货0xFF:通信故障 
*****************************************************************************/
uint LifterCheckRstTask(uchar binNo,uchar *errNo)
{
	uchar rst = 0;
	rst = LifterSend(VMC_VENDINGRESULT_REQ,binNo,0,0);
	if(rst == 0)
		return 0xFF;
	if(rst == 2)
		return 0;
	if(ackTask.Param[4] == GCC_VENDINGRESULT_ACK)//成功收到ACK
	{	
		if(ackTask.Param[5] == 0x01)//正在出货
			return 2;
		else
		{
			if(ackTask.Param[6] == 0x00)//出货成功
			{
				*errNo = 0;
				return 1;
			}
			else
			{
				*errNo = ackTask.Param[7];
				return 1;
			}
				
		}
	}
	return 0;
	
}



/*****************************************************************************
** Function name:	LifterSendCheckState	
** Descriptions:	      	发送查询状态命令													 			
** parameters:		binNo:柜号
** outparameters:      0:无故障
					1:整机状态故障
					2:升降机故障
					3:取货电机故障
					4:用户电动门故障
					5:弹币机构故障
					6:防盗模块故障
					7:货到控制器故障
					8:货道大门故障
** Returned value:	0整机空闲 1整机忙2查询失败0xFF通信失败
*****************************************************************************/
uint LifterCheckStateTask(uchar binNo,uchar *errNo)
{
	uchar rst = 0;
	rst = LifterSend(VMC_STATUS_REQ,binNo,0,0);
	if(rst == 0)
		return 0xFF;
	if(rst == 2)
		return 2;
	
	if(ackTask.Param[4] == GCC_STATUS_ACK)//成功收到ACK
	{	
		if(ackTask.Param[5] == 0)//整机标志 0空闲 1忙
		{
			*errNo = 0;
			return 0;
		}
		else
		{
			*errNo = 1;
			if(ackTask.Param[6] & 0x01) *errNo = 1;//整机状态
			if(ackTask.Param[7] & 0x01) *errNo = 2;//升降机
			if(ackTask.Param[8] & 0x01) *errNo = 3;//取货电机
			if(ackTask.Param[9] & 0x01) *errNo = 4;//用户电动门
			if(ackTask.Param[10] & 0x01) *errNo = 5;//弹币机构
			if(ackTask.Param[11] & 0x01) *errNo = 6;//防盗模块
			if(ackTask.Param[12] & 0x01) *errNo = 7;//货到控制器
			if(ackTask.Param[13] & 0x01) *errNo = 8;//货道大门	
			return 1;
		}
		
	}
	return 2;
	
}

/*****************************************************************************
** Function name:	LifterSendVendout	
** Descriptions:	      	发送出货命令													 			
** parameters:		binNo:柜号
					rowNo:层号
					columnNo:道号
** Returned value:	0:发送失败; 1:发送成功;0xFF 通信失败
*****************************************************************************/
uchar LifterSendVendoutTask(uchar binNo,uchar rowNo,uchar columnNo)
{
	uchar rst = 0,rcx = 2,errNo;
	while(rcx--)
	{
		rst = LifterSend(VMC_VENDING_REQ,binNo,rowNo,columnNo);
		if(rst == 0 || rst == 2)
			return 0xFF;
		if(ackTask.Param[4] == GCC_VENDING_ACK)//成功收到ACK
		{
			return 1;
		}
		
		//如果没有收到ACK则需要在发送一次查询命令
		//确保出货命令发送成功避免重复发送出货命令
		rst = LifterCheckRstTask(binNo,&errNo);
		if(rst == 2 || rst == 1)//表示机器繁忙 同样意味着出货命令发送成功
			return 1;
	}
	return 0;
}



/*****************************************************************************
** Function name:	LifterVendoutTask	
** Descriptions:														 			
** parameters:		无				
** Returned value:	01:成功
					11:数据错误 
					12:无货
					13:卡货		  
					14:取货门没开启
					15:货物未取走
					16:货柜大门被打开
					17:升降机构故障
					21:整机故障
					22:升降机故障
					23:取货电机故障
					24:用户电动门故障
					25:弹币机构故障
					26:防盗模块故障
					27:货到控制器故障
					28:货道大门故�
					51:状态查询超时
					52:出货命令发送失败
					53:出货结果查询超时
					99:其他故障
					255(0xFF):通信失败
*******************************************************************************/

uchar LifterVendoutTask(struct COMMTASK xdata*liftPtr)
{
	uchar res = 0,rcx = 5;
	uchar rowNo = 0,columnNo = 0,binNum,errNo,state;
	unsigned short takeTimeOut = 10,vendoutTimeout;
	binNum = liftPtr->Param[0];
	rowNo = liftPtr->Param[1] / 10 ;
	columnNo = liftPtr->Param[1] % 10;
	
	//开始出货 //查询状态是否正常，正常出货
	rcx = 10;
	while(rcx--)
	{
		state = LifterCheckStateTask(binNum,&errNo); 	
		if(state == 0)//正常就跳开
			break;
		else if(state == 1)//机器忙
		{
			if(errNo & 0xFF)//有故障
			{
				if(errNo == 1) return 21;
				if(errNo == 2) return 22;
				if(errNo == 3) return 23;
				if(errNo == 4) return 24;
				if(errNo == 5) return 25;
				if(errNo == 6) return 26;
				if(errNo == 7) return 27;
				if(errNo == 8) return 28;
				return 99;
					
			}
		}
		else if(state == 0xFF)
		{
			rcx = 1;
		}
		lifterDelay(2000);
	}

	if(state != 0)//超时查询升降机仍然不正常
	{
		if(state == 0xFF)
			return 0xFF;
		else
			return 51;
	}
		
	while(1)
	{
		//进行出货操作
		res = LifterSendVendoutTask(binNum,rowNo,columnNo);
		if(res == 0 || res == 0xFF)
			return 52;
		
		lifterDelay(500);
		//5000 * 10  = 50000 
		vendoutTimeout = 100 +(8 - rowNo) * 10;//检测出货结果超时
		while(vendoutTimeout--)
		{	
			state = LifterCheckRstTask(binNum,&errNo);//检测出货结果
			if(state == 0x01)//出货完成
			{
				break;
			}
			lifterDelay(1000);
		}
		
		if(vendoutTimeout)
		{
			if(errNo != 0)//出货失败
			{
				if((errNo == 5) && takeTimeOut)//货未取走
				{
					lifterDelay(2000);
					takeTimeOut--;
					continue;
				}
				//return ((stateH > 7) ? (stateH + 10) : 99);
				return (errNo + 10);
			}	
			else//出货成功
				return 1;
		}
		else										   	
			return 53;		
	}

}

uchar LifterVendout(uchar binNum,uchar logicNo)
{
	uchar timeout = 0,comOK = 0,index = 0;

	memset( &lifterTask, 0, sizeof( struct COMMTASK ) );
	Channel.CommState	= COMM_BUZY;
	lifterTask.Id		= ID_CHANNEL;
	lifterTask.Cmd		= VMC_VENDING_REQ;
	lifterTask.Priority = PRIORITY_NORMAL;
	lifterTask.Param[0] = binNum;
	lifterTask.Param[1] = logicNo;
	ZhkSchedulerAddTask( &lifterTask );
	
	while(! ( timeout || comOK ))
	{
		WaitForWork( 100, NULL );
		timeout = TestDeviceTimeOut( &Channel );
		comOK = TestDeviceCommOK( &Channel );	
	}

	if(timeout)
		return 0xFF;
	if(comOK)
		return Channel.State;	
	return 0xFF;
}








