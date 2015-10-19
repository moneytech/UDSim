#include "gamedata.h"

GameData::GameData() {

}

GameData::~GameData() {

}

Module *GameData::get_module(int id) {
  for(vector<Module>::iterator it = modules.begin(); it != modules.end(); ++it) {
    if(it->getArbId() == id) return &*it;
  }  
  return NULL;
}

vector <Module *> GameData::get_active_modules() {
  vector<Module *> active_modules;
  for(vector<Module>::iterator it = modules.begin(); it != modules.end(); ++it) {
    if(it->isResponder() == false) active_modules.push_back(&*it);
  }  
  return active_modules;
}

/* Same as get_active_modules but designed for learning mode */
vector <Module *> GameData::get_possible_active_modules() {
  vector<Module *> possible_active_modules;
  for(vector<Module>::iterator it = possible_modules.begin(); it != possible_modules.end(); ++it) {
    if(it->isResponder() == false) possible_active_modules.push_back(&*it);
  }  
  return possible_active_modules;
}


Module *GameData::get_possible_module(int id) {
  for(vector<Module>::iterator it = possible_modules.begin(); it != possible_modules.end(); ++it) {
    if(it->getArbId() == id) return &*it;
  }  
  return NULL;
}

void GameData::setMode(int m) {
  switch(m) {
    case MODE_SIM:
      Msg("Switching to Simulator mode");
      if(_gui) _gui->setStatus("Simulation Mode");
      if(mode == MODE_LEARN) { // Previous mode was learning, update
        Msg("Normalizing learned data");
        GameData::processLearned();
      }
      mode=m;
      if(_gui) _gui->DrawModules();
      break;
    case MODE_LEARN:
      Msg("Switching to Learning mode");
      if(_gui) _gui->setStatus("Learning Mode");
      mode=m;
      break;
    case MODE_ATTACK:
      Msg("Switching to Attack mode");
      if(_gui) _gui->setStatus("Attack Mode");
      if(mode == MODE_LEARN) { // Previous mode was learning, update
        Msg("Normalizing learned data");
        GameData::processLearned();
      }
      mode=m;
      if(_gui) _gui->DrawModules();
      break;
    default:
      Msg("Unknown game mode");
      break;
  }
}

void GameData::processPkt(canfd_frame *cf) {
  switch(mode) {
    case MODE_SIM:
      GameData::HandleSim(cf);
      break;
    case MODE_LEARN:
      GameData::LearnPacket(cf);
      break;
    case MODE_ATTACK:
      break;
    default:
      cout << "ERROR: Processing packets while in an unknown mode" << mode << endl;
      break;
  }
}

void GameData::HandleSim(canfd_frame *cf) {

}

void GameData::LearnPacket(canfd_frame *cf) {
  Module *module = GameData::get_possible_module(cf->can_id);
  Module *possible_module = GameData::isPossibleISOTP(cf);
  int possible;
  if(module) {
    module->addPacket(cf);
    if(possible_module) {
      module->incMatchedISOTP();
    } else {
      // Still maybe an ISOTP answer, check for common syntax
      if(cf->data[0] == 0x10 && cf->len == 8) {
        module->incMatchedISOTP();
      } else if(cf->data[0] == 0x30 && cf->len == 3) {
        module->incMatchedISOTP();
      } else if(cf->data[0] >= 0x21 || cf->data[0] <= 0x30) {
        module->incMatchedISOTP();
      } else {
        module->incMissedISOTP();
      }
    }
    module->setState(STATE_ACTIVE);
    if(_gui) _gui->DrawModules();
  } else if(possible_module) { // Haven't seen this ID yet
    possible_module->addPacket(cf);
    possible_modules.push_back(*possible_module);
    if(_gui) _gui->DrawModules();
  }
}

Module *GameData::isPossibleISOTP(canfd_frame *cf) {
  int i;
  bool padding = false;
  char last_byte;
  Module *possible = NULL;
    if(cf->data[0] == cf->len - 1) { // Possible UDS request
      possible = new Module(cf->can_id);
    } else if(cf->data[0] < cf->len - 2) { // Check if remaining bytes are just padding
      padding = true;
      if(cf->data[0] == 0) padding = false;
      last_byte = cf->data[cf->data[0] + 1];
      for(i=cf->data[0] + 2; i < cf->len; i++) {
        if(cf->data[i] != last_byte) {
          padding = false;
        } else {
          last_byte = cf->data[i];
        }
      }
      if(padding == true) { // Possible UDS w/ padding
        possible = new Module(cf->can_id);
        possible->setPaddingByte(last_byte);
      }
    }
  return possible;
}

void GameData::processLearned() {
  if(verbose) cout << "Identified " << possible_modules.size() << " possible modules" << endl;
  for(vector<Module>::iterator it = possible_modules.begin(); it != possible_modules.end(); ++it) {
    if(it->confidence() > CONFIDENCE_THRESHOLD) {
      if(verbose) cout << "ID: " << hex << it->getArbId() << " Looks like a UDS compliant module" << endl;
      modules.push_back(*it);
    }
  } 
  if(verbose) cout << "Locating responders" << endl;
  Module *responder = NULL;
  for(vector<Module>::iterator it = modules.begin(); it != modules.end(); ++it) {
     if(it->isResponder() == false) {
       responder = GameData::get_module(it->getArbId() + 0x300);
       if(responder) { // GM style positive response
         it->setPositiveResponderID(responder->getArbId());
         responder->setResponder(true);
       }
       responder = GameData::get_module(it->getArbId() + 0x400);
       if(responder) { // GM style negative response
         it->setNegativeResponderID(responder->getArbId());
         responder->setResponder(true);
       }
       responder = GameData::get_module(it->getArbId() + 0x08);
       if(responder) { // Standard response
         it->setPositiveResponderID(responder->getArbId());
         it->setNegativeResponderID(responder->getArbId());
         responder->setResponder(true);
       }
     }
  }
  stringstream m;
  m << "Identified " << GameData::get_active_modules().size() << " Active modules";
  GameData::Msg(m.str());
  possible_modules.clear();
}

string GameData::frame2string(canfd_frame *cf) {
  stringstream pkt;
  if(cf->len < 0 || cf->len > 8) { 
    return "ERROR: CAN packet with imporoper length";
  }
  pkt << hex << cf->can_id << CANID_DELIM;
  int i;
  for(i=0; i < cf->len; i++) {
    pkt << setfill('0') << setw(2) << hex << (int)cf->data[i];
  }
  return pkt.str();
}

void GameData::Msg(string mesg) {
  if(_gui == NULL) return;
  _gui->Msg(mesg);
}

bool GameData::SaveConfig() {
  ofstream configFile;
  configFile.open("config_data.cfg");
  // Globals
  // Modules
  configFile << endl;
  for(vector<Module>::iterator it = modules.begin(); it != modules.end(); ++it) {
    configFile << "[" << hex << it->getArbId() << "]" << endl;
    configFile << "pos = " << dec << it->getX() << "," << it->getY() << endl;
    configFile << "responder = " << it->isResponder() << endl;
    if(!it->isResponder()) {
      if(it->getPositiveResponder() != -1) configFile << "possitiveID = " << hex << it->getPositiveResponder() << endl;
      if(it->getNegativeResponder() != -1) configFile << "negativeID = " << hex << it->getNegativeResponder() << endl;
    }
    configFile << "{Packets}" << endl;
    vector <CanFrame *>frames = it->getHistory();
    for(vector<CanFrame *>::iterator it2 = frames.begin(); it2 != frames.end(); ++it2) {
      CanFrame *frame = *it2;
      configFile << frame->str() << endl;
    }
    configFile << endl;
  }
  configFile.close();
  Msg("Saved config_data.cfg");
  return true;
}

void GameData::nextMode() {
  switch(mode) {
    case MODE_SIM:
      GameData::setMode(MODE_LEARN);
      break;
    case MODE_LEARN:
      GameData::setMode(MODE_ATTACK);
      break;
    case MODE_ATTACK:
      GameData::setMode(MODE_SIM);
      break;
  }
}

int GameData::string2hex(string s) {
  stringstream ss;
  int h;
  ss << hex << s;
  ss >> h;
  return h;
}

int GameData::string2int(string s) {
  stringstream ss;
  int i;
  ss << dec << s;
  ss >> i;
  return i;
}

void GameData::processCan() {
  struct canfd_frame cf;
  int i;
  if(!canif) return;
  vector <CanFrame *>frames = canif->getPackets();
  for(vector <CanFrame *>::iterator it=frames.begin(); it != frames.end(); ++it) {
    CanFrame *pkt = *it;
    if(verbose) Msg(pkt->str());
    cf.can_id = pkt->can_id;
    cf.len = pkt->len;
    for(i=0; i < pkt->len; i++) {
      cf.data[i] = pkt->data[i];
    }
    GameData::processPkt(&cf);
  }
}
