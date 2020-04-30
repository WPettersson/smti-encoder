#include <CoinPackedMatrix.hpp>
#include <CoinPackedVector.hpp>
#include "smti.h"

#define DEBUG_IP_MODEL

void SMTI::IP_Model::add_single_constraints() {
  // Single stability constraints
  for(auto & [key, one]: _parent->_ones) {
    for(int two_id: one.prefs()) {
      const Agent & two = _parent->_twos.at(two_id);
      std::map<int, double> con;
      // First sum
      for(auto other: one.as_good_as(two)) {
        con[_lr.at(one.id()).at(other)] -= 1;
      }
      // Second sum
      for(auto other: two.as_good_as(one)) {
        con[_lr.at(other).at(two.id())] -= 1;
      }
      CoinPackedVector coincon;
      for(auto [conkey, value]: con) {
        coincon.insert(conkey, value);
      }
      _constraints.appendRow(coincon);
      // Want 1 - first_sum <= second sum
      // 1 <= first_sum + second_sum
      // -1 >= -first_sum - second_sum
      _rhs.push_back(-1);
      _lhs.push_back(-1.0 * _solverInterface.getInfinity());
    }
  }
}

void SMTI::IP_Model::add_merged_constraints() {

  // For a two.id(), and an index into two's preference groups,
  // better_than[two.id()][index] is a list of all variables corresponding to
  // matches "at least as good as" any in the given group of twos preferences.
  // Note that the indices to better_than (both of them) are going to have an
  // off-by-one, as variables have to start at 1 (for SAT-solver reasons) but
  // vectors start at 0.
  std::map<int, std::map<int, std::list<int>>> better_than;

  // Fill better_than
  for(const auto & [key, one]: _parent->_ones) {
    for(std::vector<signed int> tie: one.preferences()) {
      for(auto pref: tie) {
        int rank = _parent->_twos.at(pref).rank_of(one.id());
        for(size_t l = rank; l <= _parent->_twos.at(pref).preferences().size(); ++l) {
          better_than[pref][l].push_back(_lr.at(one.id()).at(pref));
        }
      }
    }
  }

  for(auto & [key, one]: _parent->_ones) {
    int group = 0;
    // For these merged constraints, the left sum is going to be over "options
    // that one has, that are better than two". Thus, as we progress down the
    // preference list of one, we can slowly build up the list of variables in
    // the left sum.
    std::list<int> left;
    for(const std::vector<signed int> & tie: one.preferences()) {
      // The variables on the right sum
      std::list<int> right;
      for(signed int pref: tie) {
        left.push_back(_lr.at(one.id()).at(pref));
        int rank = _parent->_twos.at(pref).rank_of(one.id());
        for(auto & var: better_than[pref][rank]) {
          right.push_back(var);
        }
      }
      // size * ( 1 - left_side) <= right_side
      // size - size * left <= right
      // -right - size*left <= -size
      std::map<int, double> con;
      for(auto & l : left) {
        con[l] -= tie.size();
      }
      for(auto & r : right) {
        con[r] -= 1;
      }
      CoinPackedVector coincon;
      for(auto [conkey, value]: con) {
        coincon.insert(conkey, value);
      }
      _constraints.appendRow(coincon);
      _rhs.push_back(-1.0 * tie.size());
      _lhs.push_back(-1.0 * _solverInterface.getInfinity());
      group++;
    }
  }
}


Matching SMTI::solve(bool optimise, bool merged) const {
  IP_Model model(this);
  return model.solve(optimise, merged);
}

Matching SMTI::IP_Model::solve(bool optimise, bool merged){
  int num_cols = 0;
  for(auto & [key, one]: _parent->_ones) {
    for(int two_id: one.prefs()) {
#ifdef DEBUG_IP_MODEL
      std::cout << "Mapping " << one.id() << " with " << two_id << " is " << num_cols << std::endl;
#endif
      _lr[one.id()][two_id] = num_cols;
      num_cols += 1;
    }
  }
  _constraints.setDimensions(0, num_cols);
  double* col_lb = new double[num_cols];
  double* col_ub = new double[num_cols];
  for(int i = 0; i < num_cols; ++i) {
    col_lb[i] = 0;
    col_ub[i] = 1;
  }
  // Ones capacity
  for(auto & [key, one]: _parent->_ones) {
    if (one.prefs().size() == 0) {
      continue;
    }
    CoinPackedVector con;
    for(int two_id: one.prefs()) {
      con.insert(_lr[one.id()][two_id], 1);
    }
    _rhs.push_back(1);
    _constraints.appendRow(con);
    if (optimise) {
      _lhs.push_back(0);
    } else {
      // Not optimising, aka solving for COM-SMTI, so each agent must be
      // matched exactly once.
      _lhs.push_back(1);
    }
  }
  // Twos capacity
  for(auto & [key, two]: _parent->_twos) {
    if (two.prefs().size() == 0) {
      continue;
    }
    CoinPackedVector con;
    for(int one_id: two.prefs()) {
      con.insert(_lr[one_id][two.id()], 1);
    }
    _rhs.push_back(1);
    _lhs.push_back(0);
    _constraints.appendRow(con);
  }
  if (!merged) {
    add_single_constraints();
  } else {
    add_merged_constraints();
  }
  double* objective = new double[_constraints.getNumCols()];
  for(int i = 0; i < _constraints.getNumCols(); ++i) {
    objective[i] = 1;
  }

  // Now we have to convert a std::list to a double[]
  double* my_lhs = new double[_constraints.getNumRows()];
  double* my_rhs = new double[_constraints.getNumRows()];
  {
    // Fill inside a block so that the scope of i is minimised
    int i = 0;
    for(auto left: _lhs) {
      my_lhs[i] = left;
      i++;
    }
    i = 0;
    for(auto right: _rhs) {
      my_rhs[i] = right;
      i++;
    }
  }
#ifdef DEBUG_IP_MODEL
  for(int row = 0; row < _constraints.getNumRows(); ++row) {
    std::cout << my_lhs[row] << " ≤";
    for(int col = 0; col < _constraints.getNumCols(); ++col) {
      if (_constraints.getCoefficient(row, col) != 0) {
        std::cout << " " << _constraints.getCoefficient(row, col) << "x" << col;
      }
    }
    std::cout << " ≤ " << my_rhs[row] << std::endl;
  }
  for(int col = 0; col < _constraints.getNumCols(); ++col) {
    std::cout << col_lb[col] << " ≤ " << "x" << col << " ≤ " << col_ub[col] << std::endl;
  }
#endif
  _solverInterface.loadProblem(_constraints, col_lb, col_ub, objective, my_lhs, my_rhs);
  for(int i = 0; i < _constraints.getNumCols(); ++i) {
    _solverInterface.setInteger(i);
  }
  _solverInterface.setObjSense(-1.0); // -1.0 is maximise, 1.0 is minimise
  _solverInterface.initialSolve();
  std::list<std::pair<int, int>> result;
  for(auto [left_id, right_map]: _lr) {
    for(auto [right_id, var_id]: right_map) {
      if (_solverInterface.getColSolution()[var_id] >= 1.0 - epsilon) {
        result.push_back(std::make_pair(left_id, right_id));
      }
    }
  }
  delete[] col_lb;
  delete[] col_ub;
  delete[] objective;
  delete[] my_lhs;
  delete[] my_rhs;
  return result;
}