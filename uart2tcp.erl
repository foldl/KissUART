-module(uart2tcp).

-export([start/2]).

start(UartPort, TcpPort) when is_integer(TcpPort) -> 
    start(UartPort, {{127,0,0,1}, TcpPort});
start(UartPort, {Ip, TcpPort}) when is_integer(TcpPort) -> 
    spawn_link(fun () -> start0(UartPort, {Ip, TcpPort}) end).

-define(command_write_to_uart ,     0).
-define(command_read_from_uart,     1).
-define(command_dbg_msg       ,     2).
-define(command_shutdown      ,     3).

-record(state, {
          socket,
          pid
         }).

-define(UART_SETTING,  [{baud, 115200}, {stopbits, 1}, {databits, 8}]).
-define(show_progress, io:format(".", [])).

-define(log(Fmt, Args), io:format(Fmt, Args)).

start0(UartPort, {Ip, TcpPort}) ->
    Opts = [binary, {packet, raw}, {ip, Ip}, {active, false}],
    case gen_tcp:listen(TcpPort, Opts) of
        {ok, Listen_socket} ->
            accept_loop(Listen_socket, [UartPort]);
        {error, Reason} ->
            {stop, Reason}
    end.

accept_loop(LSock, [UartPort]) ->
    case gen_tcp:accept(LSock) of
        {ok, Socket} ->
            ?log("connected~n",[]),
            {ok, Pid} = open(Socket, UartPort),           
            gen_tcp:controlling_process(Socket, Pid),
            inet:setopts(Socket, [{active, true}]),
            receive
                {stop, Pid, _Reason} -> 
                    gen_tcp:close(LSock),
                    ok
            end;
        {error, Reason} ->
            {error, Reason}
    end.

open(Socket, DeviceNo) when is_integer(DeviceNo) -> 
    init(DeviceNo, Socket, def_setting(DeviceNo)).

def_setting(_) -> ?UART_SETTING.

init(DeviceNo, Socket, Opts) ->
    ExtPrg = case os:type() of
        {win32, _} ->
            [$" | filename:join(filename:dirname(code:which(?MODULE)), "uart_port.exe\" ")];
        OSType ->
            throw(OSType)
    end,
    PidX = self(),
    Args = build_args(Opts, [" -port " ++ integer_to_list(DeviceNo)]),
    Pid = spawn_link(fun () ->
            process_flag(trap_exit, true),
            Port = open_port({spawn, ExtPrg ++ Args}, [{packet, 2}, binary]),
            loop(Port, #state{socket = Socket, pid = PidX})
        end),
    {ok, Pid}.

report_stop(#state{pid = Pid} = _State, Reason) -> Pid ! {stop, self(), Reason}.

loop(Port, #state{socket = Socket} = State) ->
    receive
        {Port, {data, <<?command_read_from_uart, Data/binary>>}} ->
            ?show_progress,
            gen_tcp:send(Socket, Data),
            loop(Port, State);
        {Port, {data, <<?command_dbg_msg, Str/binary>>}} ->
            io:format("COM DBG: ~s~n", [Str]),
            loop(Port, State);
        {tcp, Socket, Bin} ->
            ?show_progress,
            Port ! {self(), {command, [?command_write_to_uart, Bin]}},  
            loop(Port, State);
        stop ->
            Port ! {self(), {command, [?command_shutdown]}},
            Port ! {self(), close},
            gen_tcp:close(Socket),
            report_stop(State, stop);
        {Port, closed} ->
            gen_tcp:close(Socket),
            report_stop(State, port);
        {tcp_closed, Socket} ->
            ?log("~nclosed~n", []),
            Port ! {self(), {command, [?command_shutdown]}},
            Port ! {self(), close},
            report_stop(State, tcp_closed);
        {'EXIT', _Pid, _Reason} ->
            Port ! {self(), {command, [?command_shutdown]}},
            Port ! {self(), close},
            gen_tcp:close(Socket),
            exit(port_terminated);
        X ->
            io:format("unknow message: ~p~n", [X]),
            loop(Port, State)
    end.

build_args([{baud, V} | Opts], Acc) ->
    build_args(Opts, [" -baud ", integer_to_list(V) | Acc]);
build_args([{databits, V} | Opts], Acc) ->
    build_args(Opts, [" -databits ", integer_to_list(V) | Acc]);
build_args([{stopbits, V} | Opts], Acc) ->
    build_args(Opts, [" -stopbits ", integer_to_list(V) | Acc]);
build_args([{parity, V} | Opts], Acc) ->
    build_args(Opts, [" -parity ", atom_to_list(V) | Acc]);
build_args([_X | Opts], Acc) ->
    build_args(Opts, Acc);
build_args([], Acc) ->
    lists:concat(Acc).

