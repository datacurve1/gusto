#pragma once
#include <cstdint>
#include <vector>
namespace gusto::client::rof2 {
struct TagMesh {
  struct Vertex {
    float x, y, z;
    std::uint32_t color;
  };
  std::vector<Vertex> vertices;
  std::vector<std::int16_t> indices;
};
class TagModels {
public:
  enum class Shape { Arrow, Octagon, Paw };
  using ArrowVertex = TagMesh::Vertex;
  TagModels();
  TagMesh Get(Shape shape, std::uint32_t color) const;

private:
  void CalculateShapes();
  void CalculateArrowVertices();
  void CalculateOctagonVertices();
  void CalculatePawVertices();
  void AppendArrowIndices();
  void AppendOctagonIndices();
  void AppendPawIndices();
  std::vector<ArrowVertex> arrow_vertices, octagon_vertices, paw_vertices;
  std::vector<std::int16_t> indices;
  unsigned arrow_index_start = 0, arrow_primitive_count = 0;
  unsigned octagon_index_start = 0, octagon_primitive_count = 0;
  unsigned paw_index_start = 0, paw_primitive_count = 0;
};
} // namespace gusto::client::rof2
