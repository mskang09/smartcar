#include <stdio.h>
#include <unistd.h>   
#include <stdlib.h>   
#include <string.h> 
#include <arpa/inet.h>                              //- socket 통신 관련 라이브러리
#include <wiringPi.h> 
#include <wiringSerial.h>
#include <softPwm.h>

//- 센서 연결 핀 번호 선언 --------------------------------------------------------------------------------------------------------------------------
#define IN1_PIN						1				 //- DC모터 
#define IN2_PIN						4
#define IN3_PIN						5
#define IN4_PIN						6

#define LEFT_TRACER_PIN				10			     //- IRTracer 
#define RIGHT_TRACER_PIN			11


#define TCP_PORT             	    9120             //- TCP Socket 통신 포트

//- 동작 모드 ----------------------------------------------------------------------------------------------------------------------------------------------------------
#define RUN_LEFTGO	    			'1'
#define RUN_GO	        			'2'
#define RUN_RIGHTGO	    			'3'
#define RUN_STOP	    			'5'
#define RUN_LEFTBACK				'7'
#define RUN_BACK	    			'8'
#define RUN_RIGHTBACK     			'9'

//- DC모터 속도 관련 상수 선언 -----------------------------------------------------------------------------------------------------------------
#define SPEED   		             40


//- 자동 & 수동 동작 모드 상수  선언 ---------------------------- -------------------------------------------------------------------------------
#define MODE_MANUAL     			'M'
#define MODE_AUTO       	 		'A'

#define CONN_OK                		'1'
#define CONN_DIS                	'0'
#define CONN_NONE            		'2'

//- 모터 동작 값  선언 ------------------------------------------------------------------------------------------------------------------------------------------
#define INIT_VALUE  				SPEED,  SPEED,  SPEED,   SPEED,          "INIT"
#define GO_VALUE  		    	    SPEED,  0,         SPEED ,   0,          "GO"
#define BACK_VALUE  				0,         SPEED,  0,           SPEED,   "BACK"
#define LEFT_VALUE  				0,         SPEED,  SPEED,    0,          "LEFT"
#define RIGHT_VALUE  			    SPEED,  0,         0,           SPEED,   "RIGHT"
#define STOP_VALUE  				0,         0,          0,          0,    "STOP"

#define START_IDX                  1

//- 실시간 동영상 송신을 위한 서버 제어 명령어 상수 -------------------------------------------------------------------------------------------------------------------------------
#define UV4L_START					"sudo service uv4l_raspicam restart"
#define UV4L_STOP					"pkill uv4l"

//- 함수 선언 -----------------------------------------------------------------------------------------------------------------------------------------------------------
void hw_init();
void initServer();
void parserCmd();
void initDCMotor();
void controlMotor(int _IN1, int _IN2, int _IN3, int _IN4, char *msg);
void setRunDirection();
void setConnection();
void setRunMode();
void controlCar();
void autoControl();


//- 전역 변수 및 상수 ------------------------------------------------------------------------------------------------------------------------------------
struct sockaddr_in 	client_addr, server_addr;   //- 소켓 주소 정보 저장 변수
int       			ssock,  csock=-1;           //- Server, Client 소켓 핸들 저장 변수
socklen_t  			clen;
char 				rx_buf[BUFSIZ] = "";
int 				recv_size;
int					gRunDirection  = RUN_STOP;
int					gRunMode       = MODE_MANUAL;
char				gConnect       = CONN_NONE;


//- 엔트리 포인터 -------------------------------------------------------------------------------------------------------------------------------------
int main()
{
    unsigned int nNextTime=0, nCheckTime=0;
	
    initServer();                         	//- WiFi 초기화
    hw_init();                           	//- 라이브러리, 센서 초기화
    nNextTime = millis();               	//- 일정시간 간격 저장 변수

    WAIT_CLIENT:
		 printf("wait client .....\n");
		if (listen(ssock, 8) < 0) perror("listen()");
		clen = sizeof(client_addr);
		while(csock == -1) {
				csock  = accept(ssock, (struct sockaddr *)&client_addr, &clen);
				printf("connect client %d\n", csock);
				gConnect = CONN_OK;
				system(UV4L_START);          //- UV4L Server 시작
				if (csock)  break;
		}
		
    
    while(csock != -1)
    {
			//- 0.25초 간격으로 WiFi로 수신된 데이터 여부 체크 후 읽어오기
			if(millis() > nNextTime + 250)
			{
				   if((recv_size = recv(csock, rx_buf, BUFSIZ, MSG_DONTWAIT)) > 0)
				   {
						rx_buf[recv_size] = '\0';
						if(rx_buf[0] == '@')  parserCmd();
					}
					nNextTime = millis();
					controlCar();
					
					if(gConnect == CONN_DIS) {
						close(csock);
						system(UV4L_STOP);          //- UV4L Server 종료
						delay(1000);
						csock = -1;
						break;
					}
			}
    }
    printf ("END\n") ;
    goto WAIT_CLIENT;
    return 0 ;
}

//- 라이브러리 및 센서 초기화 -----------------------------------------------------------------------------------------------------------------
void hw_init(void)
{
    wiringPiSetup();
    
    initDCMotor();

	
    
    //- 적외선트레이서  초기화
	pinMode(LEFT_TRACER_PIN,  INPUT);
	pinMode(RIGHT_TRACER_PIN, INPUT);

}


//- WiFi 통신 초기화 -----------------------------------------------------------------------------------------------------------------------------
void initServer() 
{
    if ((ssock = socket(AF_INET, SOCK_STREAM, 0)) < 0) perror("socket()");

    bzero(&server_addr, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons (TCP_PORT);
    

    if (bind(ssock, (struct sockaddr *)&server_addr, sizeof(server_addr))<0)
	  perror("bind()");
	  
	  printf("server init OK\n");
	  
}

//- WiFi로부터 수신된 데이터 분석 후 처리 ------------------------------------------------------------------------------------------
void parserCmd()
{
	    printf("RX Data = %s\n", rx_buf);
	    
		if(!strncmp(&rx_buf[START_IDX], "CMD", 3)) {  
				setRunDirection();
				
		}else 	if(!strncmp(&rx_buf[START_IDX], "MOD", 3)) {  
				setRunMode();
		}else 	if(!strncmp(&rx_buf[START_IDX], "CON", 3)) {  
				setConnection();
		}
}


//- 자동차 이동 방향 설정  -----------------------------------------------------------------------------------------------------------------------------------
void setRunDirection(){

        char dirValue = -1;
        
	    if(rx_buf[START_IDX+3] == ',') 
        {
			 dirValue = rx_buf[START_IDX+4];
			 
			if(rx_buf[START_IDX+5] == '#')
					  gRunDirection= dirValue;   	
	   }
	   printf("setRunDirection() = dirValue = %c\n", dirValue);
}


//- 자동 & 수동 동작 모드 설정 ------------------------------------------------------------------------------------------------------------------------------
void setRunMode(){

        char modeValue = -1;
        
	    if(rx_buf[START_IDX+3] == ',') 
        {
			 modeValue = rx_buf[START_IDX+4];
			 
			if(rx_buf[START_IDX+5] == '#'){
					  gRunMode= modeValue;   	
					  gRunDirection= RUN_STOP;   	
					  controlMotor(STOP_VALUE); 
		      }
	   }
	   printf("setRunMode() = gRunMode = %c\n", modeValue);
}

//- 자동차 이동 방향 설정  -----------------------------------------------------------------------------------------------------------------------------------
void setConnection(){

        char conValue = -1;
        
	    if(rx_buf[START_IDX+3] == ',') 
        {
			 conValue = rx_buf[START_IDX+4];
			 
			if(rx_buf[START_IDX+5] == '#')
			     gConnect = conValue;
	   }
	   printf("setConnection() = gConnect = %c\n", gConnect);
}


//- 자동차 제어 --------------------------- ------------------------------------------------------------------------------------------------------------------------------
void controlCar(){

	if(gRunMode == MODE_MANUAL)
	{
			switch(gRunDirection)
			{
					case  RUN_LEFTGO:  		controlMotor(LEFT_VALUE); 	  delay(300);   gRunDirection =  RUN_GO;       break;
					case  RUN_GO:	        controlMotor(GO_VALUE);       break;
					case  RUN_RIGHTGO:	    controlMotor(RIGHT_VALUE); 	  delay(300);   gRunDirection =  RUN_GO;       break;
					case  RUN_STOP:	    	controlMotor(STOP_VALUE); 	  break;
					case  RUN_LEFTBACK:		controlMotor(LEFT_VALUE); 	  delay(300);   gRunDirection =  RUN_BACK;     break;
					case  RUN_BACK:    		controlMotor(BACK_VALUE); 	  break;
					case  RUN_RIGHTBACK:    controlMotor(RIGHT_VALUE); 	  delay(300);   gRunDirection =  RUN_BACK;     break;
			}
			
	}else if(gRunMode == MODE_AUTO){
		        
		   autoControl();
	}
}

//- IRTracer에 의한 자동 제어 ------------------------------------------------------------------------------------------------------
void autoControl(){
	int  nLValue = digitalRead(LEFT_TRACER_PIN);
	int  nRValue = digitalRead(RIGHT_TRACER_PIN);
	
	printf("LTracer - %d, RTracer - %d\n", nLValue,  nRValue) ;	
	
	if( (nLValue == HIGH) && (nRValue == HIGH))
	{
		    printf(" ALL detect ~!!!  MOVE  ");
			controlMotor(GO_VALUE);
	}else if( nLValue == HIGH){
		    printf(" LEFT detect ~!!! MOVE  ");
			controlMotor(LEFT_VALUE);
			delay(300);
			controlMotor(GO_VALUE);
    }else if(nRValue == HIGH){
		   printf(" RIGHT detect ~!!! MOVE  ");
	       controlMotor(RIGHT_VALUE);
	       	delay(300);
			controlMotor(GO_VALUE);
	}else{
		   controlMotor(STOP_VALUE);	
	}		
}


//---------------------------------------------------------------------------------------------------------------------------------------------------------------------
//- 모터 제어 관련 함수
//---------------------------------------------------------------------------------------------------------------------------------------------------------------------
//- 모터 초기화 -------------------------------------------------------------------------------------------------------------------------------------------
void initDCMotor()
{
		pinMode(IN1_PIN, SOFT_PWM_OUTPUT);
		pinMode(IN2_PIN, SOFT_PWM_OUTPUT);
		pinMode(IN3_PIN, SOFT_PWM_OUTPUT);
		pinMode(IN4_PIN, SOFT_PWM_OUTPUT);
	
		softPwmCreate(IN1_PIN, 0, SPEED);
		softPwmCreate(IN2_PIN, 0, SPEED);
		softPwmCreate(IN3_PIN, 0, SPEED);
		softPwmCreate(IN4_PIN, 0, SPEED);		
}

//- 모터 제어 -------------------------------------------------------------------------------------------------------------------------------------------
void controlMotor(int _IN1, int _IN2, int _IN3, int _IN4, char *msg)
{
		softPwmWrite(IN1_PIN, _IN1);
		softPwmWrite(IN2_PIN, _IN2);
		softPwmWrite(IN3_PIN, _IN3);
		softPwmWrite(IN4_PIN, _IN4);			
		printf("STATE - %s\n", msg) ;			
}

