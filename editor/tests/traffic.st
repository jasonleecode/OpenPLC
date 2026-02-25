PROGRAM TrafficLight

VAR
    (* 控制 *)
    Run         : BOOL := TRUE;

    (* 状态变量 *)
    State       : INT := 1;

    (* 定时器 *)
    T           : TON;

    (* 输出 *)
    NS_G        : BOOL;
    NS_Y        : BOOL;
    NS_R        : BOOL;

    EW_G        : BOOL;
    EW_Y        : BOOL;
    EW_R        : BOOL;
END_VAR


(* 默认全部关闭 *)
NS_G := FALSE;
NS_Y := FALSE;
NS_R := FALSE;
EW_G := FALSE;
EW_Y := FALSE;
EW_R := FALSE;


IF Run THEN

    CASE State OF

        1:  (* NS 绿灯 20秒 *)
            NS_G := TRUE;
            EW_R := TRUE;

            T(IN := TRUE, PT := T#20s);

            IF T.Q THEN
                T(IN := FALSE);
                State := 2;
            END_IF;


        2:  (* NS 黄灯 3秒 *)
            NS_Y := TRUE;
            EW_R := TRUE;

            T(IN := TRUE, PT := T#3s);

            IF T.Q THEN
                T(IN := FALSE);
                State := 3;
            END_IF;


        3:  (* 全红 2秒 *)
            NS_R := TRUE;
            EW_R := TRUE;

            T(IN := TRUE, PT := T#2s);

            IF T.Q THEN
                T(IN := FALSE);
                State := 4;
            END_IF;


        4:  (* EW 绿灯 20秒 *)
            EW_G := TRUE;
            NS_R := TRUE;

            T(IN := TRUE, PT := T#20s);

            IF T.Q THEN
                T(IN := FALSE);
                State := 5;
            END_IF;


        5:  (* EW 黄灯 3秒 *)
            EW_Y := TRUE;
            NS_R := TRUE;

            T(IN := TRUE, PT := T#3s);

            IF T.Q THEN
                T(IN := FALSE);
                State := 6;
            END_IF;


        6:  (* 全红 2秒 *)
            NS_R := TRUE;
            EW_R := TRUE;

            T(IN := TRUE, PT := T#2s);

            IF T.Q THEN
                T(IN := FALSE);
                State := 1;
            END_IF;

    END_CASE;

END_IF;

END_PROGRAM