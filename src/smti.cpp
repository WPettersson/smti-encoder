#include <fstream>
#include <sstream>
#include <set>

#include "smti.h"

SMTI::SMTI(int size, int pref_length, float tie_density, std::mt19937 & generator) :
  _size(size), _num_dummies(0) {
  // Construct the list of preferences. This list is constantly shuffled to
  // create preferences for each agent.
  Agent::preference_options.clear();
  for(int i = 1; i <= size; ++i) {
    Agent::preference_options.push_back(i);
  }

  std::vector<std::vector<int>> two_prefs(size+1);
  for(int i = 1; i <= size; ++i) {
    _ones.emplace(i, Agent(i, pref_length, tie_density, generator));
    for(auto pref: _ones.at(i).prefs()) {
      two_prefs[pref].push_back(i);
    }
  }
  for(int i = 1; i <= size; ++i) {
    _twos.emplace(i, Agent(i, two_prefs[i], tie_density, generator));
  }
}


SMTI::SMTI(std::string filename) : _num_dummies(0) {
  std::ifstream infile(filename);
  std::string line;
  getline(infile, line);
  _size = std::stoi(line);
  bool expect_capacity = false;
  if (_size == 0) {
    // Probably reading a file in format for 1810.02711
    getline(infile, line);
    _size = std::stoi(line);
    expect_capacity = true;
  }
  getline(infile, line);
  int second_size = std::stoi(line);

  for(int lineno = 0; lineno < _size; ++lineno) {
    int id;
    getline(infile, line);
    std::stringstream prefstream(line);
    prefstream >> id;
    std::string token;
    std::vector<std::vector<int>> preferences;
    std::vector<int> tie;
    bool in_tie = false;
    while (prefstream >> token) {
      if (token == ":") {
        continue;
      }
      if ((token.front() == '[') || (token.front() == '('))  {
        token.erase(0, 1); // Remove [
        in_tie = true;
      }
      if ((token.back() == ']')  || (token.back() == ')')) {
        token.pop_back();
        int i = std::stoi(token);
        tie.push_back(i);
        preferences.push_back(std::move(tie));
        tie = std::vector<int>();
        in_tie = false;
      } else if (in_tie) {
        int i = std::stoi(token);
        tie.push_back(i);
      } else { // Not in tie
        int i = std::stoi(token);
        std::vector<int> here;
        here.push_back(i);
        preferences.push_back(std::move(here));
      }
    }
    _ones.emplace(id, Agent(id, preferences));
  }
  for(int lineno = 0; lineno < second_size; ++lineno) {
    int id;
    getline(infile, line);
    std::stringstream prefstream(line);
    prefstream >> id;
    std::string token;
    std::vector<std::vector<int>> preferences;
    std::vector<int> tie;
    bool in_tie = false;
    bool need_capacity = expect_capacity;
    while (prefstream >> token) {
      if (token == ":") {
        continue;
      }
      if (need_capacity) {
        // We just pulled in the capacity, which is hopefully 1, but we ignore
        // it.
        need_capacity = false;
        continue;
      }
      if ((token.front() == '[') || (token.front() == '(')) {
        token.erase(0, 1); // Remove [
        in_tie = true;
      }
      if ((token.back() == ']') || (token.back() == ')')) {
        token.pop_back();
        int i = std::stoi(token);
        tie.push_back(i);
        preferences.push_back(std::move(tie));
        tie = std::vector<int>();
        in_tie = false;
      } else if (in_tie) {
        int i = std::stoi(token);
        tie.push_back(i);
      } else { // Not in tie
        int i = std::stoi(token);
        std::vector<int> here;
        here.push_back(i);
        preferences.push_back(std::move(here));
      }
    }
    _twos.emplace(id, Agent(id, preferences));
  }
}

SMTI::SMTI(const std::vector<std::vector<std::vector<int>>> & ones, const std::vector<std::vector<std::vector<int>>> & twos) :
  _ones(), _twos() {
  _size = ones.size();
  for (unsigned int id = 0; id < ones.size(); ++id) {
    _ones.emplace(id, Agent(id, ones.at(id)));
  }
  for (unsigned int id = 0; id < twos.size(); ++id) {
    _twos.emplace(id, Agent(id, twos[id]));
  }
}

void SMTI::add_dummy(int num_dummy) {
  // Create the preferences of each dummy.
  std::vector<int> dummy_prefs;
  for(int i = 1; i <= _size; ++i) {
    dummy_prefs.push_back(i);
  }
  std::vector<std::vector<int>> prefs;
  prefs.push_back(dummy_prefs);

  // Create and add the actual dummy agents
  for(int i = 1; i <= num_dummy; ++i) {
    _ones.emplace(_size + i, Agent(_size + i, prefs, true));
    _twos.emplace(_size + i, Agent(_size + i, prefs, true));
  }

  // Add the dummies as compatible to all existing agents.
  for(auto & [key, one]: _ones) {
    one.add_dummy_pref_up_to(_size + 1, _size + num_dummy);
  }
  for(auto & [key, two]: _twos) {
    two.add_dummy_pref_up_to(_size + 1, _size + num_dummy);
  }
  _size += num_dummy;
  _num_dummies += num_dummy;
}

void SMTI::remove_dummy(int num_dummy) {
  // Remove the dummies.
  for(int i = 0; i < num_dummy; ++i) {
    _ones.erase(_size - i);
    _twos.erase(_size - i);
  }
  // Remove them as preference options.
  for(auto & [key, one]: _ones) {
    one.remove_dummies(num_dummy);
  }
  for(auto & [key, two]: _twos) {
    two.remove_dummies(num_dummy);
  }
  _size -= num_dummy;
  _num_dummies -= num_dummy;
}

std::string SMTI::encodeSAT() {
  make_var_map();
  std::stringstream ss;
  int num_clauses = 0;
  // Clause 1
  for (auto & [key, one]: _ones) {
    ss << _one_vars[std::make_tuple(one.id(), 1)] << " 0" << std::endl;
    num_clauses++;
    ss << "-" << _one_vars[std::make_tuple(one.id(), one.num_prefs() + 1)] <<
          " 0" << std::endl;
    num_clauses++;
  }
  // Clause 2
  for (auto & [key, two]: _twos) {
    ss << _two_vars[std::make_tuple(two.id(), 1)] << " 0" << std::endl;
    num_clauses++;
    ss << "-" << _two_vars[std::make_tuple(two.id(), two.num_prefs() + 1)] <<
          " 0" << std::endl;
    num_clauses++;
  }
  // Clause 3
  for (auto & [key, one]: _ones) {
    for (size_t i = 1; i <= one.prefs().size(); ++i) {
      ss << _one_vars[std::make_tuple(one.id(), i)] << " -" <<
           (_one_vars[std::make_tuple(one.id(), i)]+1) << " 0" << std::endl;
      num_clauses++;
    }
  }
  // Clause 4
  for (auto & [key, two]: _twos) {
    for (size_t i = 1; i <= two.prefs().size(); ++i) {
      ss << _two_vars[std::make_tuple(two.id(), i)] << " -" <<
           (_two_vars[std::make_tuple(two.id(), i)]+1) << " 0" << std::endl;
      num_clauses++;
    }
  }
  for (auto & [key, one]: _ones) {
    for (auto two_id: one.prefs()) {
      Agent &two = _twos.at(two_id);
      int p = one.position_of(two);
      if (p == -1) {
        continue;
      }
      int q = two.position_of(one);
      if (q == -1) {
        continue;
      }
      // Clause 5
      ss << "-" << _one_vars[std::make_tuple(one.id(), p)] << " " <<
         (_one_vars[std::make_tuple(one.id(), p)]+1) << " " <<
         _two_vars[std::make_tuple(two.id(), q)] << " 0" << std::endl;
      num_clauses++;
      ss << "-" << _one_vars[std::make_tuple(one.id(), p)] << " " <<
         (_one_vars[std::make_tuple(one.id(), p)]+1) << " -" <<
         (_two_vars[std::make_tuple(two.id(), q)]+1) << " 0" << std::endl;
      num_clauses++;
      // Clause 6
      ss << "-" << _two_vars[std::make_tuple(two.id(), q)] << " " <<
         (_two_vars[std::make_tuple(two.id(), q)]+1) << " " <<
         _one_vars[std::make_tuple(one.id(), p)] << " 0" << std::endl;
      num_clauses++;
      ss << "-" << _two_vars[std::make_tuple(two.id(), q)] << " " <<
         (_two_vars[std::make_tuple(two.id(), q)]+1) << " -" <<
         (_one_vars[std::make_tuple(one.id(), p)]+1) << " 0" << std::endl;
      num_clauses++;
      int pplus = one.position_of_next_worst(two);
      if (pplus < 0) {
        continue;
      }
      int qplus = two.position_of_next_worst(one);
      if (qplus < 0) {
        continue;
      }
      // Clause 7
      ss << "-" << _one_vars[std::make_tuple(one.id(), pplus)] << " -" <<
        _two_vars[std::make_tuple(two.id(), qplus)] << " 0" << std::endl;
      num_clauses++;
      // Clause 8
      ss << "-" << _two_vars[std::make_tuple(two.id(), qplus)] << " -" <<
        _one_vars[std::make_tuple(one.id(), pplus)] << " 0" << std::endl;
      num_clauses++;
    }
  }
  std::stringstream start;
  start << "p cnf " << (_one_vars.size() + _two_vars.size()) << " " << num_clauses;
  start << std::endl << ss.str();
  return start.str();
}

std::string SMTI::encodeMZN(bool optimise) {
  std::stringstream ss;
  std::vector<std::string> vars;
  for(auto & [key, one]: _ones) {
    for(auto & [twokey, two]: _twos) {
      if (one.is_compatible(two)) {
        std::string name = "x" + std::to_string(one.id()) + "_" + std::to_string(two.id());
        ss << "var 0..1: " << name << ";" << std::endl;
        vars.push_back(std::move(name));
      }
    }
  }

  // Ones capacity
  for(auto & [key, one]: _ones) {
    if (one.prefs().size() == 0) {
      continue;
    }
    ss << "constraint ";
    bool first = true;
    for(int two_id: one.prefs()) {
      if (! first) {
        ss << " + ";
      }
      first = false;
      ss << "x" << one.id() << "_" << two_id;
    }
    if (optimise) {
      ss << " <= 1;" << std::endl;
    } else {
      ss << " = 1;" << std::endl;
    }
  }
  // Twos capacity
  for(auto & [key, two]: _twos) {
    if (two.prefs().size() == 0) {
      continue;
    }
    ss << "constraint ";
    bool first = true;
    for(int one_id: two.prefs()) {
      if (! first) {
        ss << " + ";
      }
      first = false;
      ss << "x" << one_id << "_" << two.id();
    }
    ss << " <= 1;" << std::endl;
  }
  // Stability constraints
  for(auto & [key, one]: _ones) {
    for(int two_id: one.prefs()) {
      Agent & two = _twos.at(two_id);
      ss << "constraint 1 - (";
      bool first = true;
      for(auto other: one.as_good_as(two)) {
        if (! first) {
          ss << " + ";
        }
        first = false;
        ss << "x" << one.id() << "_" << other;
      }
      ss << ") <= (";
      first = true;
      for(auto other: two.as_good_as(one)) {
        if (! first) {
          ss << " + ";
        }
        first = false;
        ss << "x" << other << "_" << two.id();
      }
      ss << ");" << std::endl;
    }
  }
  if (optimise) {
    ss << "solve maximize ";
    bool first = true;
    for(auto & name: vars) {
      if (! first) {
        ss << " + " << std::endl;
      }
      ss << name;
      first = false;
    }
    ss << ";" << std::endl;
  } else {
    ss << "solve satisfy;" << std::endl;
  }

  if (optimise) {
    ss << "output [ \"Max is \" ++ show(";
    bool first = true;
    for(auto & name: vars) {
      if (! first) {
        ss << " + " << std::endl;
      }
      ss << name;
      first = false;
    }
    ss << ")];" << std::endl;
  }
  //ss << "output [ ";
  //bool first = true;
  //for(auto & name: vars) {
  //  if (! first) {
  //    ss << " ++ " << std::endl;
  //  }
  //  ss << "if fix(" << name << " == 1) then \"" << name << ", \" else \"\" endif ";
  //  first = false;
  //}
  //ss << "];" << std::endl;
  return ss.str();
}

std::string SMTI::encodeWPMaxSAT() {
  make_var_map();
  std::stringstream ss;
  int num_clauses = 0;
  int top_weight = 500;
  // Clause 1
  for (auto & [key, one]: _ones) {
    ss << top_weight << " ";
    ss << _one_vars[std::make_tuple(one.id(), 1)] << " 0" << std::endl;
    num_clauses++;
    ss << "1 " << " ";
    ss << "-" << _one_vars[std::make_tuple(one.id(), one.num_prefs() + 1)] <<
          " 0" << std::endl;
    num_clauses++;
  }
  // Clause 2
  for (auto & [key, two]: _twos) {
    ss << top_weight << " ";
    ss << _two_vars[std::make_tuple(two.id(), 1)] << " 0" << std::endl;
    num_clauses++;
    ss << "1 " << " ";
    ss << "-" << _two_vars[std::make_tuple(two.id(), two.num_prefs() + 1)] <<
          " 0" << std::endl;
    num_clauses++;
  }
  // Clause 3
  for (auto & [key, one]: _ones) {
    for (size_t i = 1; i <= one.prefs().size(); ++i) {
      ss << top_weight << " ";
      ss << _one_vars[std::make_tuple(one.id(), i)] << " -" <<
           (_one_vars[std::make_tuple(one.id(), i)]+1) << " 0" << std::endl;
      num_clauses++;
    }
  }
  // Clause 4
  for (auto & [key, two]: _twos) {
    for (size_t i = 1; i <= two.prefs().size(); ++i) {
      ss << top_weight << " ";
      ss << _two_vars[std::make_tuple(two.id(), i)] << " -" <<
           (_two_vars[std::make_tuple(two.id(), i)]+1) << " 0" << std::endl;
      num_clauses++;
    }
  }
  for (auto & [key, one]: _ones) {
    for (auto two_id: one.prefs()) {
      Agent &two = _twos.at(two_id);
      int p = one.position_of(two);
      if (p == -1) {
        continue;
      }
      int q = two.position_of(one);
      if (q == -1) {
        continue;
      }
      // Clause 5
      ss << top_weight << " ";
      ss << "-" << _one_vars[std::make_tuple(one.id(), p)] << " " <<
         (_one_vars[std::make_tuple(one.id(), p)]+1) << " " <<
         _two_vars[std::make_tuple(two.id(), q)] << " 0" << std::endl;
      num_clauses++;
      ss << top_weight << " ";
      ss << "-" << _one_vars[std::make_tuple(one.id(), p)] << " " <<
         (_one_vars[std::make_tuple(one.id(), p)]+1) << " -" <<
         (_two_vars[std::make_tuple(two.id(), q)]+1) << " 0" << std::endl;
      num_clauses++;
      // Clause 6
      ss << top_weight << " ";
      ss << "-" << _two_vars[std::make_tuple(two.id(), q)] << " " <<
         (_two_vars[std::make_tuple(two.id(), q)]+1) << " " <<
         _one_vars[std::make_tuple(one.id(), p)] << " 0" << std::endl;
      num_clauses++;
      ss << top_weight << " ";
      ss << "-" << _two_vars[std::make_tuple(two.id(), q)] << " " <<
         (_two_vars[std::make_tuple(two.id(), q)]+1) << " -" <<
         (_one_vars[std::make_tuple(one.id(), p)]+1) << " 0" << std::endl;
      num_clauses++;
      int pplus = one.position_of_next_worst(two);
      if (pplus < 0) {
        continue;
      }
      int qplus = two.position_of_next_worst(one);
      if (qplus < 0) {
        continue;
      }
      // Clause 7
      ss << top_weight << " ";
      ss << "-" << _one_vars[std::make_tuple(one.id(), pplus)] << " -" <<
        _two_vars[std::make_tuple(two.id(), qplus)] << " 0" << std::endl;
      num_clauses++;
      // Clause 8
      ss << top_weight << " ";
      ss << "-" << _two_vars[std::make_tuple(two.id(), qplus)] << " -" <<
        _one_vars[std::make_tuple(one.id(), pplus)] << " 0" << std::endl;
      num_clauses++;
    }
  }
  std::stringstream start;
  start << "p wcnf " << (_one_vars.size() + _two_vars.size()) << " " << num_clauses;
  start << " " << top_weight;
  start << std::endl << ss.str();
  return start.str();
}

std::string SMTI::encodePBO() {
  make_var_map();
  std::stringstream ss;
  int num_clauses = 0;
  // Clause 1
  for (auto & [key, one]: _ones) {
    ss << "1 x" << _one_vars[std::make_tuple(one.id(), 1)] << " = 1;" << std::endl;
    num_clauses++;
  }
  // Clause 2
  for (auto & [key, two]: _twos) {
    ss << "1 x" << _two_vars[std::make_tuple(two.id(), 1)] << " = 1;" << std::endl;
    num_clauses++;
  }
  // Clause 3
  for (auto & [key, one]: _ones) {
    for (size_t i = 1; i <= one.prefs().size(); ++i) {
      ss << "1 x" << _one_vars[std::make_tuple(one.id(), i)] << " 1 ~x" <<
            (_one_vars[std::make_tuple(one.id(), i)]+1) << " >= 1;" << std::endl;
      num_clauses++;
    }
  }
  // Clause 4
  for (auto & [key, two]: _twos) {
    for (size_t i = 1; i <= two.prefs().size(); ++i) {
      ss << "1 x" << _two_vars[std::make_tuple(two.id(), i)] << " 1 ~x" <<
            (_two_vars[std::make_tuple(two.id(), i)]+1) << " >= 1;" << std::endl;
      num_clauses++;
    }
  }
  for (auto & [key, one]: _ones) {
    for (auto two_id: one.prefs()) {
      Agent &two = _twos.at(two_id);
      int p = one.position_of(two);
      if (p == -1) {
        continue;
      }
      int q = two.position_of(one);
      if (q == -1) {
        continue;
      }
      // Clause 5
      ss << "1 ~x" << _one_vars[std::make_tuple(one.id(), p)] << " 1 x" <<
         (_one_vars[std::make_tuple(one.id(), p+1)]) << " 1 x" <<
         _two_vars[std::make_tuple(two.id(), q)] << " >= 1;" << std::endl;
      num_clauses++;
      ss << "1 ~x" << _one_vars[std::make_tuple(one.id(), p)] << " 1 x" <<
         (_one_vars[std::make_tuple(one.id(), p+1)]) << " 1 ~x" <<
         (_two_vars[std::make_tuple(two.id(), q+1)]) << " >= 1;" << std::endl;
      num_clauses++;
      // Clause 6
      ss << "1 ~x" << _two_vars[std::make_tuple(two.id(), q)] << " 1 x" <<
         (_two_vars[std::make_tuple(two.id(), q+1)]) << " 1 x" <<
         _one_vars[std::make_tuple(one.id(), p)] << " >= 1;" << std::endl;
      num_clauses++;
      ss << "1 ~x" << _two_vars[std::make_tuple(two.id(), q)] << " 1 x" <<
         (_two_vars[std::make_tuple(two.id(), q+1)]) << " 1 ~x" <<
         (_one_vars[std::make_tuple(one.id(), p+1)]) << " >= 1;" << std::endl;
      num_clauses++;
      int pplus = one.position_of_next_worst(two);
      if (pplus < 0) {
        continue;
      }
      int qplus = two.position_of_next_worst(one);
      if (qplus < 0) {
        continue;
      }
      // Clause 7
      ss << "1 ~x" << _one_vars[std::make_tuple(one.id(), pplus)] << " 1 ~x" <<
        _two_vars[std::make_tuple(two.id(), qplus)] << " >= 1;" << std::endl;
      num_clauses++;
      // Clause 8
      ss << "1 ~x" << _two_vars[std::make_tuple(two.id(), qplus)] << " 1 ~x" <<
        _one_vars[std::make_tuple(one.id(), pplus)] << " >= 1;" << std::endl;
      num_clauses++;
    }
  }
  std::stringstream start;
  start << "* #variable= " << (_one_vars.size() + _two_vars.size()) << " #constraint= " << num_clauses;
  // npSolver needs at least one more comment line. I don't know why, but
  // deleting it makes npSolver crash.
  start << std::endl << "* silly comment" << std::endl;
  for (auto & [key, one]: _ones) {
    int pref_length = 1;
    for(auto & pref: one.prefs()) {
      start << "* " << one.id() << " with " << pref << " is " <<_one_vars[std::make_tuple(one.id(), pref_length)] << std::endl;
      pref_length++;
    }
    start << "* " << one.id() << " unassigned is " <<_one_vars[std::make_tuple(one.id(), pref_length)] << std::endl;
  }
  for (auto & [key, two]: _twos) {
    int pref_length = 1;
    for(auto & pref: two.prefs()) {
      start << "* " << pref << " with " << two.id() << " is " <<_two_vars[std::make_tuple(two.id(), pref_length)] << std::endl;
      pref_length++;
    }
    start << "* " << two.id() << " unassigned is " <<_two_vars[std::make_tuple(two.id(), pref_length)] << std::endl;
  }
  start << "min:";
  for (auto & [key, one]: _ones) {
    start << " 1 x" << _one_vars[std::make_tuple(one.id(), one.num_prefs() + 1)];
  }
  for (auto & [key, two]: _twos) {
    start << " 1 x" << _two_vars[std::make_tuple(two.id(), two.num_prefs() + 1)];
  }
  start << " ;" << std::endl;
  start << ss.str();
  return start.str();
}

std::string SMTI::encodePBO2(bool merged) {
  int cons = 0;
  std::stringstream ss;
  std::map<int, std::map<int, int>> vars_lr;
  std::map<int, int> dummy_l;
  std::map<int, std::map<int, int>> vars_rl;
  std::map<int, int> dummy_r;
  std::vector<int> everything;
  int nvars = 0;
  for(auto & [key, one]: _ones) {
    for(int two_id: one.prefs()) {
      if (vars_lr.count(one.id()) == 0) {
        vars_lr.emplace(one.id(), std::map<int, int>());
      }
      if (vars_rl.count(two_id) == 0) {
        vars_rl.emplace(two_id, std::map<int, int>());
      }
      int var = ++nvars;
      vars_lr[one.id()][two_id] = var;
      vars_rl[two_id][one.id()] = var;
    }
  }
  for (auto & [key, one]: _ones) {
    dummy_l[one.id()] = ++nvars;
  }
  for (auto & [key, two]: _twos) {
    dummy_r[two.id()] = ++nvars;
  }
  // Ones capacity
  for(auto & [key, one]: _ones) {
    for(int two_id: one.prefs()) {
      ss << "1 x" << vars_lr[one.id()][two_id] << " ";
    }
    ss << "1 x" << dummy_l[one.id()] << " ";
    ss << " = 1;" << std::endl;
    cons++;
  }
  // Twos capacity
  for(auto & [key, two]: _twos) {
    for(int one_id: two.prefs()) {
      ss << "1 x" << vars_lr[one_id][two.id()] << " ";
    }
    ss << "1 x" << dummy_r[two.id()] << " ";
    ss << " = 1;" << std::endl;
    cons++;
  }
  // Stability constraints
  if (!merged) {
    for(auto & [key, one]: _ones) {
      for(int two_id: one.prefs()) {
        const Agent & two = agent_right(two_id);
        // 1 - first_sum <= second_sum
        // first_sum + second_sum >= 1.
        std::set<int> se;
        for(auto other: one.as_good_as(two)) {
          se.insert(vars_lr[one.id()][other]);
        }
        for(auto other: two.as_good_as(one)) {
          se.insert(vars_lr[other][two.id()]);
        }
        for (int var : se) ss << "1 x" << var << " ";
        ss << ">= 1;" << std::endl;
        cons++;
      }
    }
  } else {
    // For a two.id(), and an index into two's preference groups,
    // better_than[two.id()][index] is an array of all variables
    // corresponding to matches "at least as good as" any in the given group of
    // twos preferences.
    std::unordered_map<int, std::unordered_map<int, std::vector<int>>> better_than;

    // Fill the better_than array. 
    for(const auto & [key, one]: _ones) {
      for(const std::vector<signed int> & tie: one.preferences()) {
        for(auto pref: tie) {
          auto & two = _twos.at(pref);
          int rank = two.rank_of(one.id());
          for(size_t l = rank; l < two.preferences().size(); ++l) {
            // vars_lr[one.id()][pref] is at least as good as [pref][l]
            // Note pref is two.id(), l is a _one id.
            better_than[pref][l].push_back(vars_lr.at(one.id()).at(pref));
          }
        }
      }
    }

    for(auto & [key, one]: _ones) {
      int group = 0;
      std::set<int> left_side;
      for(const std::vector<signed int> & tie: one.preferences()) {
        std::set<int> right_side;
        for(signed int pref: tie) {
          left_side.insert(vars_lr.at(one.id()).at(pref));
          int rank = agent_right(pref).rank_of(one.id());
          for(auto & thing: better_than.at(pref).at(rank)) {
            right_side.insert(thing);
          }
        }
        // tie.size() <= right_side + tie.size() * left_side
        // right_side + tie.size() * left_side >= tie.size()

        // so if a variable is in the right, but not the left, it has
        // coefficient one
        for(auto var : right_side) {
          if (left_side.find(var) == left_side.end()) {
            ss << "1 x" << var << " ";
          }
        }

        // If a variable is in the left
        for(auto var : left_side) {
          // and not in the right, it has coefficient tie.size()j
          if (right_side.find(var) == right_side.end()) {
            ss << tie.size() << " x" << var << " ";
          } else {
              // And if it's in both left and right, it has
              // coefficient = tie.size() + 1
            ss << (tie.size() + 1) << " x" << var << " ";
          }
        }
        ss << " >= " << tie.size() << ";" << std::endl;
        cons++;
        group++;
      }
    }
  }
  // Redundant constraints
  // _ones.size() - sum(dummy_l) == _twos.size() - sum(dummy_r)
  if (_ones.size() >= _twos.size()) {
    for(auto & [key, one]: _ones) {
      ss << "1 x" << dummy_l[one.id()] << " ";
    }
    for(auto & [key, two]: _twos) {
      ss << "-1 x" << dummy_r[two.id()] << " ";
    }
    ss << "= " << _ones.size() - _twos.size() << ";" << std::endl;
  } else {
    for(auto & [key, one]: _ones) {
      ss << "-1 x" << dummy_l[one.id()] << " ";
    }
    for(auto & [key, two]: _twos) {
      ss << "1 x" << dummy_r[two.id()] << " ";
    }
    ss << "= " << _twos.size() - _ones.size() << ";" << std::endl;
  }
  cons++;
  std::stringstream start;
  start << "* #variable= " << nvars << " #constraint= " << cons;
  // npSolver needs at least one more comment line. I don't know why, but
  // deleting it makes npSolver crash.
  start << std::endl << "* silly comment" << std::endl;
  for (auto & [key, one]: _ones) {
    for(auto & pref: one.prefs()) {
      start << "* " << one.id() << " with " << pref << " is " << vars_lr.at(one.id()).at(pref) << std::endl;
    }
  }
  for (auto & [key, two]: _twos) {
    for(auto & pref: two.prefs()) {
      start << "* " << pref << " with " << two.id() << " is " << vars_rl.at(two.id()).at(pref) << std::endl;
    }
  }
  start << "min:";
  for (auto & [key, one]: _ones) {
    start << " 1 x" << dummy_l[one.id()];
  }
  for (auto & [key, two]: _twos) {
    start << " 1 x" << dummy_r[two.id()];
  }
  start << " ;" << std::endl;
  start << ss.str();
  return start.str();
}


void SMTI::make_var_map() {
  _one_vars = std::unordered_map<std::tuple<int,int>, int>();
  _two_vars = std::unordered_map<std::tuple<int,int>, int>();
  int counter = 1;
  for(auto & [key, one]: _ones) {
    unsigned int pref_length = 1;
    for(; pref_length < one.prefs().size(); pref_length++) {
      _one_vars[std::make_tuple(one.id(), pref_length)] = counter;
      counter++;
    }
    _one_vars[std::make_tuple(one.id(), pref_length)] = counter;
    counter++;
  }
  for(auto & [key, two]: _twos) {
    unsigned int pref_length = 1;
    for(; pref_length < two.prefs().size(); pref_length++) {
      _two_vars[std::make_tuple(two.id(), pref_length)] = counter;
      counter++;
    }
    _two_vars[std::make_tuple(two.id(), pref_length)] = counter;
    counter++;
  }
}

std::string SMTI::to_string(std::string id_sep, std::string bracket_start, std::string bracket_end) const {
  std::stringstream ss;
  ss << _ones.size() << std::endl;
  ss << _twos.size() << std::endl;
  for(auto & [key, one]: _ones) {
    ss << one.pref_list_string(id_sep, bracket_start, bracket_end) << std::endl;
  }
  for(auto & [key, two]: _twos) {
    ss << two.pref_list_string(id_sep, bracket_start, bracket_end) << std::endl;
  }
  return ss.str();
}
