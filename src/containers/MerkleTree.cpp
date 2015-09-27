
#include "MerkleTree.hpp"
#include "../Log.hpp"
#include <botan/sha2_64.h>
#include <botan/base64.h>


// records must be sorted by name
MerkleTree::MerkleTree(const std::vector<RecordPtr>& records)
{
  Log::get().notice("Building Merkle tree of size " +
                    std::to_string(records.size()));

  std::vector<NodePtr> row;
  for (auto r : records)
  {
    LeafPtr leaf = std::make_shared<Leaf>(r, nullptr);
    leaves_.push_back(leaf);
    row.push_back(leaf);
  }

  rootHash_ = buildTree(row);
}



Json::Value MerkleTree::generateSubtree(const std::string& domain) const
{
  if (leaves_.empty())
  {
    Json::Value empty;
    return empty;
  }

  LeafPtr needle = std::make_shared<Leaf>(domain);
  auto lowerBound =
      std::lower_bound(leaves_.begin(), leaves_.end(), needle, isLessThan);

  Log::get().notice("Lower bound on domain at " +
                    std::to_string(lowerBound - leaves_.begin()));

  Json::Value result;
  if (lowerBound != leaves_.end() && !isLessThan(needle, *lowerBound))
    result = generatePath(*lowerBound);  // found, so return single path
  else
    result = generateSpan(needle, lowerBound);  // not found, so return span

  return result;
}



bool MerkleTree::verifySubtree(const Json::Value&, const RecordPtr&)
{
  // todo: check
  return true;
}



bool MerkleTree::verifyRoot(const Json::Value&, const std::string&)
{
  // todo: check if base of subtree equals the string
  return true;
}



SHA384_HASH MerkleTree::getRootHash() const
{
  return rootHash_;
}



// ************************** PRIVATE METHODS **************************** //



SHA384_HASH MerkleTree::buildTree(std::vector<NodePtr>& row)
{
  // build breadth-first, row by row
  while (row.size() > 1)
  {
    std::vector<NodePtr> nextRow;
    for (size_t j = 0; j < row.size(); j += 2)
    {
      // get left and right, or get left twice if row size is odd
      NodePtr left = row[j + 0];
      NodePtr right = j + 1 < row.size() ? row[j + 1] : left;

      SHA384_HASH hash = concatenateHashes(left, right);
      NodePtr node = std::make_shared<Node>(nullptr, hash);

      nextRow.push_back(node);
      node->setChildren(left, j + 1 < row.size() ? right : nullptr);
      left->setParent(node);
      right->setParent(node);
    }

    row = nextRow;
  }

  return row[0]->getHash();
}



// returns a hash of the two nodes' values
SHA384_HASH MerkleTree::concatenateHashes(const NodePtr& a, const NodePtr& b)
{
  // hash their concatenation
  Botan::SHA_384 sha384;

  std::array<uint8_t, 2 * Const::SHA384_LEN> concat;
  memcpy(concat.data(), a->getHash().data(), Const::SHA384_LEN);
  memcpy(concat.data() + Const::SHA384_LEN, b->getHash().data(),
         Const::SHA384_LEN);

  SHA384_HASH result;
  auto hash = sha384.process(concat.data(), concat.size());
  memcpy(result.data(), hash, hash.size());

  Log::get().notice(std::to_string(hash.size()) + " | " +
                    std::to_string(Const::SHA384_LEN));

  return result;
}



Json::Value MerkleTree::generatePath(const LeafPtr& leaf) const
{
  Log::get().notice("Generating single path through Merkle tree.");

  Json::Value result;

  Json::Value leafVal;
  leafVal["name"] = leaf->getName();
  leafVal["hash"] = leaf->getBase64Hash();
  result.append(leafVal);

  NodePtr node = leaf;
  while (node)
  {
    result.append(node->asValue());
    node = node->getParent();
  }

  return result;
}



Json::Value MerkleTree::generateSpan(
    const LeafPtr& needle,
    const std::vector<LeafPtr>::const_iterator& lowerBound) const
{
  Log::get().notice("Generating span through Merkle tree.");

  auto upperBound =
      std::upper_bound(leaves_.begin(), leaves_.end(), needle, isLessThan);

  Json::Value lowerPath = generatePath(*lowerBound);
  Json::Value upperPath = generatePath(*upperBound);

  // todo: return "common" path where the two paths converge

  Json::Value result;
  result["left"] = lowerPath;
  result["right"] = upperPath;
  return result;
}



bool MerkleTree::isLessThan(const LeafPtr& a, const LeafPtr& b)
{
  return a->getName() < b->getName();
}



// ************************** SUBCLASS METHODS **************************** //



MerkleTree::Node::Node(const NodePtr& parent, const SHA384_HASH& hash)
    : parent_(parent), leftChild_(nullptr), rightChild_(nullptr), hash_(hash)
{
}



void MerkleTree::Node::setParent(const NodePtr& parent)
{
  parent_ = parent;
}



void MerkleTree::Node::setChildren(const NodePtr& left, const NodePtr& right)
{
  leftChild_ = left;
  rightChild_ = right;
}



MerkleTree::NodePtr MerkleTree::Node::getParent() const
{
  return parent_;
}



SHA384_HASH MerkleTree::Node::getHash() const
{
  return hash_;
}



std::string MerkleTree::Node::getBase64Hash() const
{
  return Botan::base64_encode(hash_.data(), Const::SHA384_LEN);
}



Json::Value MerkleTree::Node::asValue() const
{
  Json::Value value;

  if (leftChild_)
    value["left"] = leftChild_->getBase64Hash();
  if (rightChild_)
    value["right"] = rightChild_->getBase64Hash();

  return value;
}



MerkleTree::Leaf::Leaf(const RecordPtr& record, const NodePtr& parent)
    : Node(parent, record->getHash()), name_(record->getName())
{
}



MerkleTree::Leaf::Leaf(const std::string& name) : name_(name)
{
}



std::string MerkleTree::Leaf::getName() const
{
  return name_;
}
