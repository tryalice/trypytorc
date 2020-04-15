#include <string>
#include <vector>

#include <ATen/core/ivalue.h>
#include <c10/util/ArrayRef.h>
#include <torch/csrc/utils/disallow_copy.h>

namespace torch {
namespace jit {

// See Python's pickletools.py for a detailed description of each of these codes
enum class OpCode : char {
  MARK = '(',
  STOP = '.',
  POP = '0',
  POP_MARK = '1',
  DUP = '2',
  FLOAT = 'F',
  INT = 'I',
  BININT = 'J',
  BININT1 = 'K',
  LONG = 'L',
  BININT2 = 'M',
  NONE = 'N',
  PERSID = 'P',
  BINPERSID = 'Q',
  REDUCE = 'R',
  STRING = 'S',
  BINSTRING = 'T',
  SHORT_BINSTRING = 'U',
  UNICODE = 'V',
  BINUNICODE = 'X',
  APPEND = 'a',
  BUILD = 'b',
  GLOBAL = 'c',
  DICT = 'd',
  EMPTY_DICT = '}',
  APPENDS = 'e',
  GET = 'g',
  BINGET = 'h',
  INST = 'i',
  LONG_BINGET = 'j',
  LIST = 'l',
  EMPTY_LIST = ']',
  OBJ = 'o',
  PUT = 'p',
  BINPUT = 'q',
  LONG_BINPUT = 'r',
  SETITEM = 's',
  TUPLE = 't',
  EMPTY_TUPLE = ')',
  SETITEMS = 'u',
  BINFLOAT = 'G',

  // Protocol 2
  PROTO = '\x80',
  NEWOBJ = '\x81',
  EXT1 = '\x82',
  EXT2 = '\x83',
  EXT4 = '\x84',
  TUPLE1 = '\x85',
  TUPLE2 = '\x86',
  TUPLE3 = '\x87',
  NEWTRUE = '\x88',
  NEWFALSE = '\x89',
  LONG1 = '\x8a',
  LONG4 = '\x8b',

  // Protocol 3 (Python 3.x)
  BINBYTES = 'B',
  SHORT_BINBYTES = 'C',

  // Protocol 4
  SHORT_BINUNICODE = '\x8c',
  BINUNICODE8 = '\x8d',
  BINBYTES8 = '\x8e',
  EMPTY_SET = '\x8f',
  ADDITEMS = '\x90',
  FROZENSET = '\x91',
  NEWOBJ_EX = '\x92',
  STACK_GLOBAL = '\x93',
  MEMOIZE = '\x94',
  FRAME = '\x95'
};

enum PicklerClass : uint8_t {
  // A reference to the tensor table
  TENSOR = 0,
  // List[int]
  INTLIST = 1,
};

using ::c10::IValue;

class Pickler {
  TH_DISALLOW_COPY_AND_ASSIGN(Pickler);

 public:
  Pickler(std::vector<at::Tensor>* tensor_table = nullptr)
      : tensor_table_(tensor_table) {}

  const std::vector<char>& stack();

  // Push protocol onto the stack
  void start();

  // Push STOP OpCode onto the stack
  void finish();

  void addIValue(const IValue& ivalue);

  // See torch/serialization.py for details, pushes a magic number, torch
  // serialization version, and system info to the pickle archive all as
  // individual pickle programs
  void pushMetadata();

  // If more than 1 value is being added to this pickle archive, then this
  // must be called before adding any values in order to mark that they should
  // be wrapped in a tuple
  void pushTuple();
  void endTuple();

 private:
  void pushDict(const IValue& ivalue);
  void pushDouble(const IValue& ivalue);
  void pushInt(const IValue& ivalue);
  void pushIntList(const IValue& ivalue);
  void pushList(const IValue& ivalue);
  void pushLiteralTensor(const IValue& ivalue);
  void pushMemoization(const IValue& ivalue);
  void pushMemoizedString(const IValue& ivalue);
  void pushTensor(const IValue& ivalue);
  void pushTensorReference(const IValue& ivalue);
  void pushTuple(const IValue& ivalue);

  void pushBinGet(uint32_t memo_id);
  void pushClass(PicklerClass cls);
  void pushGlobal(const std::string& name);
  void pushMemoization(const void* item);
  void pushString(const std::string& string);
  void pushTensorData(const at::Tensor& tensor);

  const void* getPointer(const IValue& ivalue);

  // These convert values to bytes and add them to the stack (NB: since T is to
  // the left of a '::', its type cannot be deduced by the compiler so one must
  // explicitly instantiate the template, i.e. push<int>(int) works, push(int)
  // does not)
  template<typename T>
  void push(typename std::common_type<T>::type value) {
    const char* begin = reinterpret_cast<const char*>(&value);
    stack_.insert(stack_.end(), begin, begin + sizeof(T));
  }

  // Stack of opcodes/data
  std::vector<char> stack_;

  // Memoization of IValues that have been written (index in table is used for
  // BINPUT opcodes) to enable shared references
  std::unordered_map<const void*, uint32_t> memo_map_;

  // External table of tensors to serialize. If this is missing, then tensors
  // are serialized directly into the pickle
  std::vector<at::Tensor>* tensor_table_;

  // List of tensors to serialize in the same binary as the pickle data
  std::vector<at::Tensor> literal_tensors_;

  // TODO: only use this if necessary (add a pass to find all shared ivalues,
  // and only memoize those)
  uint32_t memo_id = 0;

  bool wrap_in_list_ = true;
};

class Unpickler {
  TH_DISALLOW_COPY_AND_ASSIGN(Unpickler);

 public:
  Unpickler(
      void* data,
      size_t size,
      const std::vector<at::Tensor>* tensor_table)
      : bytes_(static_cast<const uint8_t*>(data)),
        end_ptr_(bytes_ + size),
        tensor_table_(tensor_table),
        last_opcode_(OpCode::STOP) {}

  std::vector<IValue> parse_ivalue_list();

 private:
  // No arguments ensures that a template arugment must be specified
  // so that the number of bytes read / type read is explicit
  template <typename T>
  T read() {
    AT_CHECK(
        bytes_ + sizeof(T) <= end_ptr_,
        "Unpickler overran buffer while reading a value");
    T item;
    std::memcpy(&item, bytes_, sizeof(T));
    bytes_ += sizeof(T);
    return item;
  }

  double readFloat();
  OpCode readInstruction();
  OpCode readOpCode();
  std::string readString();
  void readList();
  void run();

  std::vector<IValue> stack_;
  std::vector<IValue> memo_table_;
  std::vector<size_t> marks_;
  const uint8_t* bytes_;
  const uint8_t* end_ptr_;
  const std::vector<at::Tensor>* tensor_table_;
  OpCode last_opcode_;
};

// returns (record_size, data_ptr) for a tensor, converting it to a CPU tensor
// if necessary
std::pair<void*, uint64_t> getWriteableTensor(const at::Tensor& tensor);

// return a unique ID for this tensor
uint64_t getTensorKey(const at::Tensor& tensor);

} // namespace jit
} // namespace torch
