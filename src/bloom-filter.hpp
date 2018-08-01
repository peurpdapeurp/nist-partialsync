#ifndef PSYNC_BLOOM_FILTER_HPP
#define PSYNC_BLOOM_FILTER_HPP

#include <string>
#include <vector>

namespace psync {

static const std::size_t bits_per_char = 0x08;
static const unsigned char bit_mask[bits_per_char] = {
                               0x01,  //00000001
                               0x02,  //00000010
                               0x04,  //00000100
                               0x08,  //00001000
                               0x10,  //00010000
                               0x20,  //00100000
                               0x40,  //01000000
                               0x80   //10000000
                             };

struct optimal_parameters_t
{
  optimal_parameters_t()
  : number_of_hashes(0),
    table_size(0)
  {}

  unsigned int number_of_hashes;
  unsigned int table_size;
};

class bloom_parameters
{
public:

  bloom_parameters();

  bool compute_optimal_parameters();

  unsigned int           minimum_size;
  unsigned int           maximum_size;
  unsigned int           minimum_number_of_hashes;
  unsigned int           maximum_number_of_hashes;
  unsigned int           projected_element_count;
  double                 false_positive_probability;
  unsigned long long int random_seed;
  optimal_parameters_t   optimal_parameters;
};

class bloom_filter
{
protected:
  typedef uint32_t bloom_type;
  typedef uint8_t cell_type;
  typedef std::vector <cell_type>::iterator Iterator;

public:
  bloom_filter();
  bloom_filter(const bloom_parameters& p);
  virtual ~bloom_filter()
  {}

  void clear();
  void insert(const std::string& key);
  bool contains(const std::string& key);
  std::vector <cell_type> table();
  void setTable(std::vector <cell_type> table);
  unsigned int getTableSize();
  Iterator begin() { return bit_table_.begin(); }
  Iterator end()   { return bit_table_.end();   }

private:
  void generate_unique_salt();
  void compute_indices(const bloom_type& hash, std::size_t& bit_index, std::size_t& bit);

private:
  std::vector <bloom_type> salt_;
  std::vector <cell_type>             bit_table_;
  unsigned int            salt_count_;
  unsigned int            table_size_; // 8 * raw_table_size;
  unsigned int            raw_table_size_;
  unsigned int            projected_element_count_;
  unsigned int            inserted_element_count_;
  unsigned long long int  random_seed_;
  double                  desired_false_positive_probability_;
};

} // namespace psync

#endif // PSYNC_BLOOM_FILTER_HPP