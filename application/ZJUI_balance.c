// User includes
#include "ZJUI_balance.h"
#include "ZJUI_linkNleg.h" 
#include "ZJUI_speed_estimation.h"

#include "bsp_dwt.h"
#include "ins_task.h"

#include "user_lib.h" // 需要用到较多三角函数

#include "general_def.h" // 通用参数，比如pi
// #include "A1_motor_drive.h"
#include "MI_motor_drive.h"
// #include "joint.h"


// User define variables
// 关节电机变量
// extern motor_send_t MotorA1_send_left;        // 左腿一号电机数据体
// extern motor_send_t MotorA1_send_right;       // 右腿一号电机数据体
// extern motor_recv_t MotorA1_recv_left_id00;   // 左腿00号电机接收数据体
// extern motor_recv_t MotorA1_recv_left_id01;   // 左腿01号电机接收数据体
// extern motor_recv_t MotorA1_recv_right_id00;  // 右腿00号电机接收数据体
// extern motor_recv_t MotorA1_recv_right_id01;  // 右腿01号电机接收数据体
// // 默认电机零位
// extern float zero_left_ID0;
// extern float zero_left_ID1;
// extern float zero_right_ID0;
// extern float zero_right_ID1;
// 轮电机变量
extern MI_Motor_s MI_Motor_ID1;              // 定义小米电机结构体1
extern MI_Motor_s MI_Motor_ID2;              // 定义小米电机结构体2
// IMU变量
extern INS_t *INS_DATA;         
// 两个腿的参数,0为左腿,1为右腿
LinkNPodParam l_side, r_side;    
ChassisParam chassis;
// DWT计时器
static float delta_t;
static uint32_t balance_dwt_cnt;



// 任务
// 查看Link2Leg() 解算是否正确
void BalanceTask()
{   
    // DWT定时器
    delta_t = DWT_GetDeltaT(&balance_dwt_cnt); 

    // 参数组装
    ParamAssemble();

    // 五连杆换算
    Link2Leg(&l_side, &chassis);
    Link2Leg(&r_side, &chassis);

    // Body 水平速度计算 (世界坐标)
    SpeedEstimation(&l_side, &r_side, &chassis, &INS_DATA, delta_t); // 目前能运行，但是待验证

    // 根据单杆计算处的角度和杆长,计算反馈增益
    CalcLQR(&l_side);
    CalcLQR(&r_side);

    // // 转向和抗劈叉 (暂时跳过)
    // SynthesizeMotion();

    // // 腿长控制,保持机体水平 (暂时跳过)
    // LegControl();

    // VMC映射成关节输出 (这只是数学结算，不包含电机控制程序)
    VMCProject(&l_side);
    VMCProject(&r_side);

    // 电机输出力矩
    MotorControl();

}


// 参数组装
static void ParamAssemble()
{
    // 传入电机参数
    // l_side.phi1  = (+(MotorA1_recv_left_id01.Pos - zero_left_ID1) + 180.0f)*DEGREE_2_RAD;     l_side.phi1_w = +MotorA1_recv_left_id00.W;
    // l_side.phi4  = (+(MotorA1_recv_left_id00.Pos - zero_left_ID0) + 0.0f)*DEGREE_2_RAD;       l_side.phi4_w = +MotorA1_recv_left_id01.W;
    l_side.w_ecd = -MI_Motor_ID2.RxCAN_info.speed;

    // r_side.phi1  = (-(MotorA1_recv_right_id01.Pos - zero_right_ID1) + 180.0f)*DEGREE_2_RAD;    r_side.phi1_w = -MotorA1_recv_right_id00.W;
    // r_side.phi4  = (-(MotorA1_recv_right_id00.Pos - zero_right_ID0) + 0.0f)*DEGREE_2_RAD;      r_side.phi4_w = -MotorA1_recv_right_id01.W; 
    r_side.w_ecd = +MI_Motor_ID1.RxCAN_info.speed; // 顺时针旋转 = +

    // 传入IMU参数
    chassis.yaw   = INS_DATA->Yaw * DEGREE_2_RAD;    chassis.wz      = INS_DATA->Gyro[Z];  
    chassis.pitch = INS_DATA->Pitch * DEGREE_2_RAD;  chassis.pitch_w = INS_DATA->Gyro[X];  
    chassis.roll  = INS_DATA->Roll * DEGREE_2_RAD;   chassis.roll_w  = INS_DATA->Gyro[Y];  

}


/**
 * @brief 根据状态反馈计算当前腿长,查表获得LQR的反馈增益,并列式计算LQR的输出
 * @note 得到的腿部力矩输出还要经过综合运动控制系统补偿后映射为两个关节电机输出
 *
 */
static void CalcLQR(LinkNPodParam *p)
{
    // static float k[12][3] = {60.071162, -57.303242, -8.802552, -0.219882, -1.390464, -0.951558, 32.409644, -23.635877, -9.253521, 12.436266, -10.318639, -7.529540, 135.956804, -124.044032, 36.093582, 13.977819, -12.325081, 3.766791, 32.632159, -39.012888, 14.483707, 7.204778, -5.973109, 1.551763, 78.723013, -73.096567, 21.701734, 63.798027, -55.564993, 15.318437, -195.813486, 140.134182, 60.699132, -18.357761, 13.165559, 2.581731};
    static float k[12][3] = {69.702762,-142.040660,-4.207897,-7.339029,-23.447057,0.722030,33.081397,-27.904707,-13.116724,27.721428,-32.372076,-8.152221,153.974674,-146.805480,46.407912,5.255443,-5.274444,1.490716,1.448103,-10.394075,20.866157,-5.077549,6.145159,2.063441,73.286899,-72.417205,24.880666,43.401631,-44.053902,17.217584,-122.870625,101.478137,107.736492,-3.391576,2.711421,2.517693};
    float T[2] = {0}; // 0 T_wheel  1 T_hip
    float l = p->leg_len;
    float lsqr = l * l;
    // float dist_limit = abs(chassis.target_dist - chassis.dist) > MAX_DIST_TRACK ? sign(chassis.target_dist - chassis.dist) * MAX_DIST_TRACK : (chassis.target_dist - chassis.dist); // todo设置值
    // float vel_limit = abs(chassis.target_v - chassis.vel) > MAX_VEL_TRACK ? sign(chassis.target_v - chassis.vel) * MAX_VEL_TRACK : (chassis.target_v - chassis.vel);
    for (uint8_t i = 0; i < 2; ++i)
    {
        uint8_t j = i * 6;
        T[i] = (k[j + 0][0] * lsqr + k[j + 0][1] * l + k[j + 0][2]) * -p->theta +
               (k[j + 1][0] * lsqr + k[j + 1][1] * l + k[j + 1][2]) * -p->theta_w +
               (k[j + 2][0] * lsqr + k[j + 2][1] * l + k[j + 2][2]) * (chassis.target_dist - chassis.dist) +
               (k[j + 3][0] * lsqr + k[j + 3][1] * l + k[j + 3][2]) * (chassis.target_v - chassis.vel) +
               (k[j + 4][0] * lsqr + k[j + 4][1] * l + k[j + 4][2]) * -chassis.pitch +
               (k[j + 5][0] * lsqr + k[j + 5][1] * l + k[j + 5][2]) * -chassis.pitch_w;
    }
    p->T_wheel = T[0]; // 输出简化髋关节数据
    p->T_hip = T[1];
}

//电机控制指令
void MotorControl()
{
    // HTMotorSetRef(lf, 0.285f * l_side.T_front);   // 根据扭矩常数计算得到的系数
    // HTMotorSetRef(lb, 0.285f * l_side.T_back);
    // HTMotorSetRef(rf, 0.285f * -r_side.T_front);
    // HTMotorSetRef(rb, 0.285f * -r_side.T_back);
    // LKMotorSetRef(l_driven, 274.348 * l_side.T_wheel);
    // LKMotorSetRef(r_driven, 274.348 * -r_side.T_wheel);

    // 别急，先用Debug看看VMC会输出多少力矩，别寄了
    static float SCALE = 0.1f;
    // modfiy_torque_cmd(&MotorA1_send_left,0,-l_side.T_front*SCALE);
    // modfiy_torque_cmd(&MotorA1_send_right,0,r_side.T_front*SCALE);
    // unitreeA1_rxtx(&huart1); 
    // unitreeA1_rxtx(&huart6);
    // osDelay(1);

    // modfiy_torque_cmd(&MotorA1_send_left,1,-l_side.T_back*SCALE);
    // modfiy_torque_cmd(&MotorA1_send_right,1,r_side.T_back*SCALE);
    // unitreeA1_rxtx(&huart1); 
    // unitreeA1_rxtx(&huart6);
    // osDelay(1);
    osDelay(2);
}