// 头文件
#include <iostream> 
#include <vector>
#include <random>
#include <cmath> 
#include <algorithm>
#include <fstream>
using namespace std;

// random_device：用于获取真随机种子
// mt19937：梅森旋转算法，一个高质量的伪随机数生成器
std::random_device rd;
std::mt19937 gen(rd());

// 目标函数
double f(double x) {
  return x + 10 * std::sin(5 * x) + 7 * std::cos(4 * x);
}

// 个体结构体 
struct Individual {
  vector<int> genes; // 基因序列（0/1）
  double fitness; // 适应度
  double value; // 解码后的实数值

  Individual(int gene_length) : genes(gene_length), fitness(0.0), value(0.0) {}
};

// 解码函数
double decode(const std::vector<int>& genes) {
  double sum = 0.0;
  int n = genes.size();
  for (int i = 0; i < n; ++i) {
    if (genes[i]) sum += std::pow(2, n - 1 - i);
  }
  double max_val = std::pow(2, n) - 1;
  return 0.0 + (9.0 - 0.0) * sum / max_val;
}

// 适应度评估
void evaluate(Individual& ind) {
  ind.value = decode(ind.genes);
  ind.fitness = f(ind.value);
}

// 初始化种群
std::vector<Individual> initPopulation(int pop_size, int gene_length) {
  std::vector<Individual> pop;
  std::uniform_int_distribution<> dis(0, 1);
  for (int i = 0; i < pop_size; ++i) {
    Individual ind(gene_length);
    for (int j = 0; j < gene_length; ++j) {
      ind.genes[j] = dis(gen);
    }
    evaluate(ind);
    pop.push_back(ind);
  }
  return pop;
}

// 锦标赛选择：随机抽取两个个体，返回较好的那个
Individual select(const std::vector<Individual>& pop) {
  std::uniform_int_distribution<> dis(0, pop.size() - 1);
  int idx1 = dis(gen);
  int idx2 = dis(gen);
  return (pop[idx1].fitness > pop[idx2].fitness) ? pop[idx1] : pop[idx2];
}

// 交叉：单点交叉 
void crossover(Individual& p1, Individual& p2) {
  std::uniform_int_distribution<> dis(0, p1.genes.size() - 1); 
  int point = dis(gen);
  for (int i = point; i < p1.genes.size(); ++i) {
    swap(p1.genes[i], p2.genes[i]);
  }
}

// 变异：位翻转
void mutate(Individual& ind, double mutation_rate) {
  std::uniform_real_distribution<> dis(0.0, 1.0);
  for (int i = 0; i < ind.genes.size(); ++i) {
    if (dis(gen) < mutation_rate) {
      ind.genes[i] = 1 - ind.genes[i];
    }
  }
}

// 主函数
int main() {
  const int POP_SIZE = 100;
  const int GENE_LENGTH = 20;
  const int GENERATIONS = 200;
  const double MUTATION_RATE = 0.05;
  const double ELITE_RATE = 0.1;

  ofstream outFile("data.txt");
  if (!outFile.is_open()) {
    cerr << "无法打开文件!" << endl;
    return 1;
  
  // 初始化种群
  auto population = initPopulation(POP_SIZE, GENE_LENGTH);
  
  // 种群迭代
  for (int gen = 0; gen < GENERATIONS; ++gen) {
    sort(population.begin(), population.end(), [](const Individual& a, const Individual& b) {
      return a.fitness > b.fitness; 
    });
  
    // 输出当代最佳
    cout << "Gen " << gen << " | Best fitness: " << population[0].fitness << " | x = " << population[0].value << endl; 
    vector<Individual> new_pop;

    // 精英保留
    int elite_count = POP_SIZE * ELITE_RATE;
    for (int i = 0; i < elite_count; ++i) {
      new_pop.push_back(population[i]);
    }

    // 生成新一代
    while (new_pop.size() < POP_SIZE) {
      Individual parent1 = select(population);
      Individual parent2 = select(population);

      crossover(parent1, parent2);
      mutate(parent1, MUTATION_RATE);
      mutate(parent2, MUTATION_RATE);
  
      evaluate(parent1);
      evaluate(parent2);
      
      new_pop.push_back(parent1);
      new_pop.push_back(parent2);
    }

    population = move(new_pop);
  }

  // 打印结果
  sort(population.begin(), population.end(), [](const Individual& a, const Individual& b) {
    return a.fitness > b.fitness; 
  }); 

  cout << "\nFinal Best: fitness = " << population[0].fitness << ", x = " << population[0].value << endl;

  return 0;
}
