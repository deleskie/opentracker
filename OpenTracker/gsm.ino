//gsm functions

void gsm_init() {
  //setup modem pins
  debug_print(F("gsm_init() started"));

  pinMode(PIN_C_PWR_GSM, OUTPUT);
  digitalWrite(PIN_C_PWR_GSM, LOW);

  pinMode(PIN_C_KILL_GSM, OUTPUT);
  digitalWrite(PIN_C_KILL_GSM, LOW);

  pinMode(PIN_STATUS_GSM, INPUT);
  pinMode(PIN_RING_GSM, INPUT);
  
  pinMode(PIN_WAKE_GSM, OUTPUT); 
  digitalWrite(PIN_WAKE_GSM, HIGH);

  gsm_open();

  debug_print(F("gsm_init() finished"));
}

void gsm_open() {
  gsm_port.begin(115200);
}

void gsm_close() {
  gsm_port.end();
}

bool gsm_power_status() {
  // help discharge floating pin, by temporarily setting as output low
  PIO_Configure(
    g_APinDescription[PIN_STATUS_GSM].pPort,
    PIO_OUTPUT_0,
    g_APinDescription[PIN_STATUS_GSM].ulPin,
    g_APinDescription[PIN_STATUS_GSM].ulPinConfiguration);
  pinMode(PIN_STATUS_GSM, INPUT);
  delay(1);
  // read modem power status
  return digitalRead(PIN_STATUS_GSM) != LOW;
}

void gsm_on() {
  //turn on the modem
  debug_print(F("gsm_on() started"));

  int k=0;
  for (;;) {
    unsigned long t = millis();
  
    if(!gsm_power_status()) { // now off, turn on
      digitalWrite(PIN_C_PWR_GSM, HIGH);
      while (!gsm_power_status() && (millis() - t < 5000))
        delay(100);
      digitalWrite(PIN_C_PWR_GSM, LOW);
      status_delay(1000);
    }
  
    // auto-baudrate
    if (gsm_send_at())
      break;
    debug_print(F("gsm_on(): failed auto-baudrate"));

    if (++k >= 5) // max attempts
      break;
      
    gsm_off(0);
    gsm_off(1);

    status_delay(1000);

    debug_print(F("gsm_on(): try again"));
    debug_print(k);
  }

  // make sure it's not sleeping
  gsm_wakeup();

  debug_print(F("gsm_on() finished"));
}

void gsm_off(int emergency) {
  //turn off the modem
  debug_print(F("gsm_off() started"));

  unsigned long t = millis();

  if(gsm_power_status()) { // now on, turn off
    digitalWrite(emergency ? PIN_C_KILL_GSM : PIN_C_PWR_GSM, HIGH);
    while (gsm_power_status() && (millis() - t < 5000))
      delay(100);
    digitalWrite(emergency ? PIN_C_KILL_GSM : PIN_C_PWR_GSM, LOW);
    status_delay(1000);
  }
  gsm_get_reply(1);

  debug_print(F("gsm_off() finished"));
}

void gsm_standby() {
  // clear wake signal
  digitalWrite(PIN_WAKE_GSM, HIGH);
  // standby GSM
  gsm_port.print("AT+CFUN=0\r");
  gsm_wait_for_reply(1,0);
  gsm_port.print("AT+QSCLK=1\r");
  gsm_wait_for_reply(1,0);
}

void gsm_wakeup() {
  // wake GSM
  digitalWrite(PIN_WAKE_GSM, LOW);
  delay(1000);
  gsm_port.print("AT+QSCLK=0\r");
  gsm_wait_for_reply(1,0);
  gsm_port.print("AT+CFUN=1\r");
  gsm_wait_for_reply(1,0);
}

void gsm_setup() {
  debug_print(F("gsm_setup() started"));

  //turn off modem
  gsm_off(1);

  //blink modem restart
  blink_start();

  //turn on modem
  gsm_on();

  //configure
  gsm_config();

  debug_print(F("gsm_setup() completed"));
}

void gsm_config() {
  //supply PIN code if needed
  gsm_set_pin();

  // wait for modem ready (status 0)
  unsigned long t = millis();
  do {
    int pas = gsm_get_modem_status();
    if(pas==0 || pas==3 || pas==4) break;
    status_delay(3000);
  }
  while (millis() - t < 60000);

  //get GSM IMEI
  gsm_get_imei();

  //misc GSM startup commands (disable echo)
  gsm_startup_cmd();

  //set GSM APN
  gsm_set_apn();
}

void gsm_set_time() {
  debug_print(F("gsm_set_time() started"));

  //setting modems clock from current time var
  gsm_port.print("AT+CCLK=\"");
  gsm_port.print(time_char);
  gsm_port.print("\"\r");

  gsm_wait_for_reply(1,0);

  debug_print(F("gsm_set_time() completed"));
}

void gsm_set_pin() {
  debug_print(F("gsm_set_pin() started"));

  for (int k=0; k<5; ++k) {
    //checking if PIN is set
    gsm_port.print("AT+CPIN?");
    gsm_port.print("\r");
  
    gsm_wait_at();
    gsm_wait_for_reply(1,1);
  
    char *tmp = strstr(modem_reply, "SIM PIN");
    if(tmp!=NULL) {
      debug_print(F("gsm_set_pin(): PIN is required"));
  
      //checking if pin is valid one
      if(config.sim_pin[0] == 255) {
        debug_print(F("gsm_set_pin(): PIN is not supplied."));
      } else {
        if(strlen(config.sim_pin) == 4) {
          debug_print(F("gsm_set_pin(): PIN supplied, sending to modem."));
  
          gsm_port.print("AT+CPIN=");
          gsm_port.print(config.sim_pin);
          gsm_port.print("\r");
  
          gsm_wait_for_reply(1,0);
  
          tmp = strstr(modem_reply, "OK");
          if(tmp!=NULL) {
            debug_print(F("gsm_set_pin(): PIN is accepted"));
          } else {
            debug_print(F("gsm_set_pin(): PIN is not accepted"));
          }
          break;
        } else {
          debug_print(F("gsm_set_pin(): PIN supplied, but has invalid length. Not sending to modem."));
          break;
        }
      }
    }
    tmp = strstr(modem_reply, "READY");
    if(tmp!=NULL) {
      debug_print(F("gsm_set_pin(): PIN is not required"));
      break;
    }
    status_delay(2000);
  }
  
  debug_print(F("gsm_set_pin() completed"));
}

void gsm_get_time() {
  debug_print(F("gsm_get_time() started"));

  //clean any serial data

  gsm_get_reply(0);

  //get time from modem
  gsm_port.print("AT+CCLK?");
  gsm_port.print("\r");

  gsm_wait_for_reply(1,1);

  char *tmp = strstr(modem_reply, "+CCLK: \"");
  tmp += strlen("+CCLK: \"");
  char *tmpval = strtok(tmp, "\"");

  //copy data to main time var
  strlcpy(time_char, tmpval, sizeof(time_char));

  debug_print(F("gsm_get_time() result:"));
  debug_print(time_char);

  debug_print(F("gsm_get_time() completed"));
}

void gsm_startup_cmd() {
  debug_print(F("gsm_startup_cmd() started"));

  //disable echo for TCP data
  gsm_port.print("AT+QISDE=0");
  gsm_port.print("\r");

  gsm_wait_for_reply(1,0);

  //set receiving TCP data by command
  gsm_port.print("AT+QINDI=1");
  gsm_port.print("\r");

  gsm_wait_for_reply(1,0);

  //set SMS as text format
  gsm_port.print("AT+CMGF=1");
  gsm_port.print("\r");

  gsm_wait_for_reply(1,0);

  debug_print(F("gsm_startup_cmd() completed"));
}

void gsm_get_imei() {
  debug_print(F("gsm_get_imei() started"));

  //get modem's imei
  gsm_port.print("AT+GSN");
  gsm_port.print("\r");

  status_delay(1000);
  gsm_get_reply(1);

  //reply data stored to modem_reply[200]
  char *tmp = strstr(modem_reply, "AT+GSN\r\r\n");
  tmp += strlen("AT+GSN\r\r\n");
  char *tmpval = strtok(tmp, "\r");

  //copy data to main IMEI var
  strlcpy(config.imei, tmpval, sizeof(config.imei));

  debug_print(F("gsm_get_imei() result:"));
  debug_print(config.imei);

  debug_print(F("gsm_get_imei() completed"));
}

int gsm_send_at() {
  debug_print(F("gsm_send_at() started"));

  int ret = 0;
  for (int k=0; k<5; ++k) {
    gsm_port.print("AT");
    gsm_port.print("\r");
    status_delay(50);
  
    gsm_get_reply(1);
    ret = (strstr(modem_reply, "AT") != NULL)
      && (strstr(modem_reply, "OK") != NULL);
    if (ret) break;

    status_delay(1000);
  }
  debug_print(F("gsm_send_at() completed"));
  debug_print(ret);
  return ret;
}

int gsm_get_modem_status() {
  debug_print(F("gsm_get_modem_status() started"));

  gsm_port.print("AT+CPAS");
  gsm_port.print("\r");

  int pas = -1; // unexpected reply
  for (int k=0; k<10; ++k) {
    status_delay(50);
    gsm_get_reply(0);
  
    char *tmp = strstr(modem_reply, "+CPAS:");
    if(tmp!=NULL) {
      pas = atoi(tmp+6);
      break;
    }
  }
  gsm_wait_for_reply(1,0);
  
  debug_print(F("gsm_get_modem_status() returned: "));
  debug_print(pas);
  return pas;
}

int gsm_disconnect() {
    int ret = 0;
    debug_print(F("gsm_disconnect() started"));
  #if GSM_DISCONNECT_AFTER_SEND
    //disconnect GSM
    gsm_port.print("AT+QIDEACT\r");
    gsm_wait_for_reply(0,0);

    //check if result contains DEACT OK
    char *tmp = strstr(modem_reply, "DEACT OK");

    if(tmp!=NULL) {
      debug_print(F("gsm_disconnect(): DEACT OK found"));
      ret = 1;
    } else {
      debug_print(F("gsm_disconnect(): DEACT OK not found."));
    }
  #else
    //close connection, if previous attempts left it open
    gsm_port.print("AT+QICLOSE\r");
    gsm_wait_for_reply(0,0);
    
    //ignore errors (will be taken care during connect)
    ret = 1;
  #endif

  debug_print(F("gsm_disconnect() completed"));
  return ret;
}

int gsm_set_apn()  {
  debug_print(F("gsm_set_apn() started"));

  //disconnect GSM
  gsm_port.print("AT+QIDEACT");
  gsm_port.print("\r");
  gsm_wait_for_reply(0,0);

  addon_event(ON_MODEM_ACTIVATION);
  
  //set all APN data, dns, etc
  gsm_port.print("AT+QIREGAPP=\"");
  gsm_port.print(config.apn);
  gsm_port.print("\",\"");
  gsm_port.print(config.user);
  gsm_port.print("\",\"");
  gsm_port.print(config.pwd);
  gsm_port.print("\"");
  gsm_port.print("\r");

  gsm_wait_for_reply(1,0);

  gsm_port.print("AT+QIDNSCFG=\"8.8.8.8\"");
  gsm_port.print("\r");

  gsm_wait_for_reply(1,0);

  gsm_port.print("AT+QIDNSIP=1");
  gsm_port.print("\r");

  gsm_wait_for_reply(1,0);

  gsm_port.print("AT+QIACT\r");

  // wait for GPRS contex activation (first time)
  unsigned long t = millis();
  do {
    gsm_wait_for_reply(1,0);
    if(modem_reply[0] != 0) break;
  }
  while (millis() - t < 60000);

  gsm_port.print("AT+QILOCIP\r"); // diagnostic only
  status_delay(500);
  gsm_get_reply(0);

  gsm_send_at();

  debug_print(F("gsm_set_apn() completed"));

  return 1;
}

int gsm_get_connection_status() {
  debug_print(F("gsm_get_connection_status() started"));
  
  gsm_port.print("AT+QISTAT\r");
  gsm_wait_for_reply(0,0);

  int ret = -1; //unknown

  if (strstr(modem_reply, "IP INITIAL") != NULL ||
    strstr(modem_reply, "IP STATUS") != NULL ||
    strstr(modem_reply, "IP CLOSE") != NULL)
    ret = 0; // ready to connect

  if (strstr(modem_reply, "CONNECT OK") != NULL)
    ret = 1; // already connected

  if (strstr(modem_reply, "TCP CONNECTING") != NULL)
    ret = 2; // previous connection failed, should close

  debug_print(F("gsm_get_connection_status() returned:"));
  debug_print(ret);
  return ret;
}

int gsm_connect() {
  int ret = 0;

  debug_print(F("gsm_connect() started"));

  //try to connect multiple times
  for(int i=0;i<CONNECT_RETRY;i++) {
    // connect only when modem is ready
    if (gsm_get_modem_status() == 0) {
      // check if connected from previous attempts
      int ipstat = gsm_get_connection_status();  

      if (ipstat > 1) {
        //close connection, if previous attempts failed
        gsm_port.print("AT+QICLOSE");
        gsm_port.print("\r");
        gsm_wait_for_reply(0,0);
        ipstat = 0;
      }
      if (ipstat < 0) {
        //deactivate required
        gsm_port.print("AT+QIDEACT");
        gsm_port.print("\r");
        gsm_wait_for_reply(0,0);
        ipstat = 0;
      }
      if (ipstat == 0) {
        debug_print(F("Connecting to remote server..."));
        debug_print(i);
    
        //open socket connection to remote host
        //opening connection
        gsm_port.print("AT+QIOPEN=\"");
        gsm_port.print(PROTO);
        gsm_port.print("\",\"");
        gsm_port.print(HOSTNAME);
        gsm_port.print("\",\"");
        gsm_port.print(HTTP_PORT);
        gsm_port.print("\"");
        gsm_port.print("\r");
    
        gsm_wait_for_reply(1, 0); // OK sent first

        long timer = millis();
        do {
          gsm_get_reply(1);
          
          if(strstr(modem_reply, "CONNECT OK")!=NULL) {
            ipstat = 1;
            break;
          }
          if(strstr(modem_reply, "CONNECT FAIL")!=NULL ||
            strstr(modem_reply, "ERROR")!=NULL) {
            ipstat = 0;
            break;
          }
          addon_delay(100);
        } while (millis() - timer < CONNECT_TIMEOUT);
      }
      
      if(ipstat == 1) {
        debug_print(F("Connected to remote server: "));
        debug_print(HOSTNAME);
  
        ret = 1;
        break;
      } else {
        debug_print(F("Can not connect to remote server: "));
        debug_print(HOSTNAME);
        // debug only:
        gsm_port.print("AT+CEER\r");
        gsm_wait_for_reply(1,0);
      }
    }

    addon_delay(2000); // wait 2s before retrying
  }
  debug_print(F("gsm_connect() completed"));
  return ret;
}

int gsm_validate_tcp() {
  char *str;
  int nonacked = 0;
  int ret = 0;

  char *tmp;
  char *tmpval;

  debug_print(F("gsm_validate_tcp() started."));

  //todo check in the loop if everything delivered
  for(int k=0;k<10;k++) {
    gsm_port.print("AT+QISACK");
    gsm_port.print("\r");

    gsm_wait_for_reply(1,0);

    //todo check if everything is delivered
    tmp = strstr(modem_reply, "+QISACK: ");
    tmp += strlen("+QISACK: ");
    tmpval = strtok(tmp, "\r");

    //checking how many bytes NON-acked
    str = strtok_r(tmpval, ", ", &tmpval);
    str = strtok_r(NULL, ", ", &tmpval);
    str = strtok_r(NULL, ", ", &tmpval);

    //non-acked value
    nonacked = atoi(str);

    if(nonacked <= PACKET_SIZE_DELIVERY) {
      //all data has been delivered to the server , if not wait and check again
      debug_print(F("gsm_validate_tcp() data delivered."));
      ret = 1;
      break;
    } else {
      debug_print(F("gsm_validate_tcp() data not yet delivered."));
    }
  }

  debug_print(F("gsm_validate_tcp() completed."));
  return ret;
}

int gsm_send_begin(int data_len) {
  //sending header packet to remote host
  gsm_port.print("AT+QISEND=");
  gsm_port.print(data_len);
  gsm_port.print("\r");

  gsm_wait_for_reply(1,0);
  if (strncmp(modem_reply, "> ", 2) == 0)
    return 1; // accepted, can send data
  return 0; // error, cannot send data
}

int gsm_send_done() {
  gsm_wait_for_reply(1,0);
  if (strncmp(modem_reply, "SEND OK", 7) == 0)
    return 1; // send successful
  return 0; // error
}

int gsm_send_http_current() {
  //send HTTP request, after connection if fully opened
  //this will send Current data

  debug_print(F("gsm_send_http(): sending current data."));
  debug_print(data_current);

  //getting length of data full package
  int http_len = strlen(config.imei)+strlen(config.key)+strlen(data_current);
  http_len = http_len+13;    //imei= &key= &d=

  debug_print(F("gsm_send_http(): Length of data packet:"));
  debug_print(http_len);

  //length of header package
  char tmp_http_len[7];
  itoa(http_len, tmp_http_len, 10);

  int tmp_len = strlen(HTTP_HEADER1)+strlen(tmp_http_len)+strlen(HTTP_HEADER2);

  addon_event(ON_SEND_DATA);
  if (gsm_get_modem_status() == 4) {
    debug_print(F("gsm_send_http(): call interrupted"));
    return 0; // abort
  }

  debug_print(F("gsm_send_http(): Length of header packet:"));
  debug_print(tmp_len);

  //sending header packet to remote host
  if (!gsm_send_begin(tmp_len)) {
    debug_print(F("gsm_send_http(): send refused"));
    return 0; // abort
  }

  //sending header
  gsm_port.print(HTTP_HEADER1);
  gsm_port.print(tmp_http_len);
  gsm_port.print(HTTP_HEADER2);

  if (!gsm_send_done()) {
    debug_print(F("gsm_send_http(): send error"));
    return 0; // abort
  }

  //validate header delivery
  gsm_validate_tcp();

  addon_event(ON_SEND_DATA);
  if (gsm_get_modem_status() == 4) {
    debug_print(F("gsm_send_http(): call interrupted"));
    return 0; // abort
  }

  debug_print(F("gsm_send_http(): Sending IMEI and Key"));
  debug_print(config.imei);
  // don't disclose the key

  //sending imei and key first
  if (!gsm_send_begin(13+strlen(config.imei)+strlen(config.key))) {
    debug_print(F("gsm_send_http(): send refused"));
    return 0; // abort
  }

  gsm_port.print("imei=");
  gsm_port.print(config.imei);
  gsm_port.print("&key=");
  gsm_port.print(config.key);
  gsm_port.print("&d=");

  if (!gsm_send_done()) {
    debug_print(F("gsm_send_http(): send error"));
    return 0; // abort
  }

  debug_print(F("gsm_send_http(): Sending body"));

  int tmp_ret = gsm_send_data_current();

  debug_print(F("gsm_send_http(): data sent."));
  return tmp_ret;
}

int gsm_send_data_current() {
  //this will send Current data

  debug_print(F("gsm_send_data_current(): sending data."));
  debug_print(data_current);

  int tmp_ret = 1; // success
  int tmp_len = strlen(data_current);
  int chunk_len;
  int chunk_pos = 0;
  int chunk_check = 0;

  if(tmp_len > PACKET_SIZE) {
    chunk_len = PACKET_SIZE;
  } else {
    chunk_len = tmp_len;
  }

  debug_print(F("gsm_send_data_current(): Body packet size:"));
  debug_print(chunk_len);

  for(int i=0;i<tmp_len;i++) {
    if((i == 0) || (chunk_pos >= PACKET_SIZE)) {
      debug_print(F("gsm_send_data_current(): Sending data chunk:"));
      debug_print(chunk_pos);

      if(chunk_pos >= PACKET_SIZE) {
        if (!gsm_send_done()) {
          debug_print(F("gsm_send_data_current(): send error"));
          return 0; // abort
        }

        //validate previous transmission
        gsm_validate_tcp();

        //next chunk, get chunk length, check if not the last one
        chunk_check = tmp_len-i;

        if(chunk_check > PACKET_SIZE) {
          chunk_len = PACKET_SIZE;
        } else {
          //last packet
          chunk_len = chunk_check;
        }

        chunk_pos = 0;
      }

      addon_event(ON_SEND_DATA);
      if (gsm_get_modem_status() == 4) {
        debug_print(F("gsm_send_data_current(): call interrupted"));
        return 0; // abort
      }

      debug_print(F("gsm_send_data_current(): chunk length:"));
      debug_print(chunk_len);

      //sending chunk
      if (!gsm_send_begin(chunk_len)) {
        debug_print(F("gsm_send_data_current(): send refused"));
        return 0; // abort
      }
    }

    //sending data
    gsm_port.print(data_current[i]);
    chunk_pos++;
  }
  
  if (!gsm_send_done()) {
    debug_print(F("gsm_send_data_current(): send error"));
    return 0; // abort
  }

  debug_print(F("gsm_send_data_current(): returned"));
  debug_print(tmp_ret);
  return tmp_ret;
}

int gsm_send_data() {
  int ret_tmp = 0;

  //send 2 ATs
  gsm_send_at();

  //make sure there is no connection
  gsm_disconnect();

  addon_event(ON_SEND_STARTED);
    
  //opening connection
  ret_tmp = gsm_connect();
  if(ret_tmp == 1) {
    //connection opened, just send data
#if SEND_RAW
      ret_tmp = gsm_send_data_current();
#else
      // send data, if ok then parse reply
      ret_tmp = gsm_send_http_current() && parse_receive_reply();
#endif
  }
  gsm_disconnect(); // always
  
  if(ret_tmp) {
    gsm_send_failures = 0;

    addon_event(ON_SEND_COMPLETED);
  } else {
    debug_print(F("Error, can not send data or no connection."));

    gsm_send_failures++;
    addon_event(ON_SEND_FAILED);
  }

  if(GSM_SEND_FAILURES_REBOOT > 0 && gsm_send_failures >= GSM_SEND_FAILURES_REBOOT) {
    power_reboot = 1;
  }

  return ret_tmp;
}

// use fullBuffer != 0 if you want to read multiple lines
void gsm_get_reply(int fullBuffer) {
  //get reply from the modem
  int index = 0;
  char inChar = 0; // Where to store the character read
  long last = millis();

  do {
    if(gsm_port.available()) {
      inChar = gsm_port.read(); // always read if available
      last = millis();
      if(index < sizeof(modem_reply)-1) { // One less than the size of the array
        modem_reply[index] = inChar; // Store it
        index++; // Increment where to write next
  
        if(index == sizeof(modem_reply)-1 || (!fullBuffer && inChar == '\n')) { //some data still available, keep it in serial buffer
          break;
        }
      }
    }
  } while(millis() - last < 10); // allow some inter-character delay

  modem_reply[index] = '\0'; // Null terminate the string

  if(index > 0) {
    debug_print(F("Modem Reply:"));
    debug_print(modem_reply);

    addon_event(ON_MODEM_REPLY);
  }
}

// use allowOK = 0 if OK comes before the end of the modem reply
void gsm_wait_for_reply(int allowOK, int fullBuffer) {
  unsigned long timeout = millis();
  
  modem_reply[0] = '\0';

  while((millis() - timeout) < (GSM_MODEM_COMMAND_TIMEOUT * 1000)) {
    gsm_get_reply(fullBuffer);
    if (gsm_is_final_result(allowOK))
      break;
    status_delay(50);
  }
  
  if (modem_reply[0] == 0) {
    debug_print(F("Warning: timed out waiting for last modem reply"));
  }
}

void gsm_wait_at() {
  unsigned long timeout = millis();

  modem_reply[0] = '\0';

  while (!strncmp(modem_reply,"AT+",3) == 0) {
    if((millis() - timeout) >= (GSM_MODEM_COMMAND_TIMEOUT * 1000)) {
      debug_print(F("Warning: timed out waiting for last modem reply"));
      break;
    }
    gsm_get_reply(0);

    status_delay(50);
  }
}

int gsm_is_final_result(int allowOK) {
  if(allowOK && strcmp(&modem_reply[strlen(modem_reply)-6],"\r\nOK\r\n") == 0) {
    return true;
  }
  #define STARTS_WITH(a, b) ( strncmp((a), (b), strlen(b)) == 0)
  switch (modem_reply[0]) {
    case '+':
      if(STARTS_WITH(&modem_reply[1], "CME ERROR:")) {
        return true;
      }
      if(STARTS_WITH(&modem_reply[1], "CMS ERROR:")) {
        return true;
      }
      if(STARTS_WITH(&modem_reply[1], "QIRD:")) {
        return true;
      }
      return false;
    case '>':
      if(strcmp(&modem_reply[1], " ") == 0) {
        return true;
      }
      return false;
    case 'A':
      if(strcmp(&modem_reply[1], "LREADY CONNECT\r\n") == 0) {
        return true;
      }
      return false;
    case 'B':
      if(strcmp(&modem_reply[1], "USY\r\n") == 0) {
        return true;
      }
      return false;
    case 'C':
      if(strcmp(&modem_reply[1], "ONNECT OK\r\n") == 0) {
        return true;
      }
      if(strcmp(&modem_reply[1], "ONNECT FAIL\r\n") == 0) {
        return true;
      }
      if(strcmp(&modem_reply[1], "LOSED\r\n") == 0) {
        return true;
      }
      if(strcmp(&modem_reply[1], "LOSE OK\r\n") == 0) {
        return true;
      }
      return false;
    case 'D':
      if(strcmp(&modem_reply[1], "EACT OK\r\n") == 0) {
        return true;
      }
      return false;
    case 'E':
      if(STARTS_WITH(&modem_reply[1], "RROR")) {
        return true;
      }
      return false;
    case 'N':
      if(strcmp(&modem_reply[1], "O ANSWER\r\n") == 0) {
        return true;
      }
      if(strcmp(&modem_reply[1], "O CARRIER\r\n") == 0) {
        return true;
      }
      if(strcmp(&modem_reply[1], "O DIALTONE\r\n") == 0) {
        return true;
      }
      return false;
    case 'O':
      if(allowOK && strcmp(&modem_reply[1], "K\r\n") == 0) {
        return true;
      }
      return false;
    case 'S':
      if(STARTS_WITH(&modem_reply[1], "END ")) {
        return true;
      }
      if(STARTS_WITH(&modem_reply[1], "TATE: ")) {
        return true;
      }
      /* no break */
      default:
        return false;
  }
}

void gsm_debug() {
  gsm_port.print("AT+QLOCKF=?");
  gsm_port.print("\r");
  status_delay(2000);
  gsm_get_reply(0);

  gsm_port.print("AT+QBAND?");
  gsm_port.print("\r");
  status_delay(2000);
  gsm_get_reply(0);

  gsm_port.print("AT+CGMR");
  gsm_port.print("\r");
  status_delay(2000);
  gsm_get_reply(0);

  gsm_port.print("AT+CGMM");
  gsm_port.print("\r");
  status_delay(2000);
  gsm_get_reply(0);

  gsm_port.print("AT+CGSN");
  gsm_port.print("\r");
  status_delay(2000);
  gsm_get_reply(0);

  gsm_port.print("AT+CREG?");
  gsm_port.print("\r");

  status_delay(2000);
  gsm_get_reply(0);

  gsm_port.print("AT+CSQ");
  gsm_port.print("\r");

  status_delay(2000);
  gsm_get_reply(0);

  gsm_port.print("AT+QENG?");
  gsm_port.print("\r");

  status_delay(2000);
  gsm_get_reply(0);

  gsm_port.print("AT+COPS?");
  gsm_port.print("\r");

  status_delay(2000);
  gsm_get_reply(0);

  gsm_port.print("AT+COPS=?");
  gsm_port.print("\r");

  status_delay(6000);
  gsm_get_reply(0);
}

#ifdef KNOWN_APN_LIST

#if KNOWN_APN_SCAN_MODE < 0 || KNOWN_APN_SCAN_MODE > 2
#error Invalid option KNOWN_APN_SCAN_MODE in tracker.h
#endif

// Use automatic APN configuration
int gsm_scan_known_apn()
{
  typedef struct
  {
    const char *apnname;
    const char *user;
    const char *pass;
    //const char *servicename;  // not required, for further expansion and requirement
    //const char *value_str;  // not required, for further expansion and requirement
  } APNSET;
  
  #define KNOWN_APN(apn,usr,pwd,isp,nul) { apn, usr, pwd/*, isp, nul*/ },
  static const APNSET apnlist[] =
  {
    KNOWN_APN_LIST
    // last element must be the default APN config
    KNOWN_APN(DEFAULT_APN, DEFAULT_USER, DEFAULT_PASS, "", NULL)
  };
  #undef KNOWN_APN
  
  enum { KNOWN_APN_COUNT = sizeof(apnlist)/sizeof(*apnlist) };
  
  int ret = 0;

  debug_print(F("gsm_scan_known_apn() started"));

  //try to connect multiple times
  for (int apn_num = 0; apn_num < KNOWN_APN_COUNT; ++apn_num)
  {
    debug_port.print(F("Testing APN: "));
    debug_print(config.apn);

    gsm_port.print("AT+QISTAT\r");
    gsm_wait_for_reply(0,0);

#if KNOWN_APN_SCAN_MODE < 2
    if (strstr(modem_reply, "IP GPRSACT") != NULL ||
      strstr(modem_reply, "IP STATUS") != NULL ||
      strstr(modem_reply, "CONNECT OK") != NULL)
#endif
    {
#if KNOWN_APN_SCAN_MODE > 0
      ret = gsm_connect();
      /*
      for (int i = 0; i < 5; i++)
      {
        debug_print(F("Connecting to remote server..."));
        debug_print(i);
  
        //open socket connection to remote host
        //opening connection
        gsm_port.print("AT+QIOPEN=\"");
        gsm_port.print(PROTO); // tcp
        gsm_port.print("\",\"");
        gsm_port.print(HOSTNAME);
        gsm_port.print("\",\"");
        gsm_port.print(HTTP_PORT);
        gsm_port.print("\"");
        gsm_port.print("\r");
  
        gsm_wait_for_reply(0, 0);
  
        char *tmp = strstr(modem_reply, "CONNECT OK");
        if (tmp != NULL)
        {
          debug_print(F("Connected to remote server: "));
          debug_print(HOSTNAME);
  
          ret = 1;
          break;
        }
        else
        {
          debug_print(F("Can not connect to remote server: "));
          debug_print(HOSTNAME);
        }
      }
      */
#else
      ret = 1;
#endif
    }
    if (ret == 1) {
      debug_print(F("gsm_scan_known_apn(): found valid APN settings"));
      break;
    }
    
    // try next APN on the list
    strlcpy(config.apn, apnlist[apn_num].apnname, sizeof(config.apn));
    strlcpy(config.user, apnlist[apn_num].user, sizeof(config.user));
    strlcpy(config.pwd, apnlist[apn_num].pass, sizeof(config.pwd));

#if KNOWN_APN_SCAN_USE_RESET
    //restart GSM with new config
    gsm_off(0);
    gsm_setup();
#else
    // apply new config
    gsm_set_apn();
#endif    
  }
  // the last APN in the array is not tested and it's applied only as default

  debug_print(F("gsm_scan_known_apn() completed"));
  return ret;
}

#endif // KNOWN_APN_LIST

