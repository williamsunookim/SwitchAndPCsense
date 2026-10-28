// SPDX-License-Identifier: MIT
#pragma once
// BTstack may deliver CAN_SEND_NOW synchronously inside request(). Arm first.
template<class Request> void request_send(bool &waiting,Request request) {
 if(waiting)return;
 waiting=true;
 if(request()!=0)waiting=false;
}
