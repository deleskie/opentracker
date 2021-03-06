
//parse remote commands from server
int parse_receive_reply() {
  //receive reply from modem and parse it
  int ret = 0;
  int len = 0;
  byte header = 0;
  int resp_code = 0;

  char *tmp;
  char *tmpcmd;
  char cmd[100] = "";  //remote commands stored here

  debug_print(F("parse_receive_reply() started"));

  addon_event(ON_RECEIVE_STARTED);
  if (gsm_get_modem_status() == 4) {
    debug_print(F("parse_receive_reply(): call interrupted"));
    return 0; // abort
  }

  for(int i=0;i<30;i++) {
    gsm_port.print("AT+QIRD=0,1,0,100");
    gsm_port.print("\r");

    gsm_wait_for_reply(1,0);

    //do we have data?
    tmp = strstr(modem_reply, "ERROR");
    if(tmp!=NULL) {
      debug_print(F("No more data available."));
      break;
    }
    
    tmp = strstr(modem_reply, "+QIRD:");
    if(tmp!=NULL) {
      tmp = strstr(modem_reply, PROTO ","); //get data length
    }
    if(tmp==NULL) {
      // no data yet, keep looking
      addon_delay(500);
      continue;
    }

    // read data length
    tmp += strlen(PROTO ",");
    len = atoi(tmp);
    debug_print(len);

    // read full buffer (data)
    gsm_get_reply(1);

    if(len==0) {
      // no data yet, keep looking
      addon_delay(500);
      continue;
    }

    // remove trailing modem response (OK)
    if (len < sizeof(modem_reply) - 1)
      modem_reply[len] = '\0';
    else
      debug_print(F("Warning: data exceeds modem receive buffer!"));

    debug_print(header);
    if (header == 0) {
      tmp = strstr(modem_reply, "HTTP/1.");
      if(tmp!=NULL) {
        debug_print(F("Found response"));
        header = 1;

        resp_code = atoi(&tmp[9]);
        debug_print(resp_code);
#if PARSE_IGNORE_COMMANDS && PARSE_IGNORE_EOF
        // optimize and close connection earlier (without reading whole reply)
        break;
#endif
      } else {
        debug_print(F("Not and HTTP response!"));
        break;
      }
    } else if (header == 1) {
      // looking for end of headers
      tmp = strstr(modem_reply, "\r\n\r\n");
      if(tmp!=NULL) {
        debug_print(F("End of header found!"));
        header = 2;

        //all data from this packet and all next packets can be commands
        tmp += strlen("\r\n\r\n");
        strlcpy(cmd, tmp, sizeof(cmd));
      }
    } else {
      // packet contains only response body
      strlcat(cmd, modem_reply, sizeof(cmd));
    }
	
    addon_event(ON_RECEIVE_DATA);
    if (gsm_get_modem_status() == 4) {
      debug_print(F("parse_receive_reply(): call interrupted"));
      return 0; // abort
    }
  }

#if SEND_RAW
  debug_print(F("RAW data mode enabled, not checking whether the packet was received or not."));
  ret = 1;

#else // HTTP

  // any http reply is valid by default
  if (header != 0)
    ret = 1;

#ifdef PARSE_ACCEPTED_RESPONSE_CODES
#define RESP_CODE(x) && (resp_code != (x))
  // apply restrictions to response code
  if (1 PARSE_ACCEPTED_RESPONSE_CODES)
    ret = 0;
#undef RESP_CODE(x)
#endif

#if !PARSE_IGNORE_EOF
  // valid only if "#eof" received
  tmp = strstr(cmd, "#eof");
  if(tmp==NULL)
    ret = 0;
#endif

#if !PARSE_IGNORE_COMMANDS
  parse_cmd(cmd);
#endif

#endif // SEND_RAW
  
  if (ret) {
    //all data was received by server
    debug_print(F("Data was fully received by the server."));
    addon_event(ON_RECEIVE_COMPLETED);
  } else {
    debug_print(F("Data was not received by the server."));
    addon_event(ON_RECEIVE_FAILED);
  }
  debug_print(F("parse_receive_reply() completed"));

  return ret;
}

void parse_cmd(char *cmd) {
  //parse commands info received from the server

  char *tmp;
  char *tmpcmd;

  debug_print(F("parse_cmd() started"));

  debug_print(F("Received commands:"));
  debug_print(cmd);

  //check for settime command (#t:13/01/11,09:43:50+00)
  tmp = strstr((cmd), "#t:");
  if(tmp!=NULL) {
    debug_print(F("Found settime command."));

    tmp = strstr(cmd, "#t:");  //\r\n\r\n
    tmp += strlen("#t:");
    tmpcmd = strtok(tmp, "\n");   //all commands end with \n

    debug_print(tmpcmd);

    if(strlen(tmpcmd) == 20) {
      debug_print(F("Valid time string found."));

      //setting current time
      strlcpy(time_char, tmpcmd, sizeof(time_char));

      gsm_set_time();
    }
  }

  debug_print(F("parse_cmd() completed"));
}
