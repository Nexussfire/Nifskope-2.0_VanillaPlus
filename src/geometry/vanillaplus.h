#ifndef VANILLAPLUS_H
#define VANILLAPLUS_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

// Temporary processing buffers only. No file or NifModel mutations.
namespace VanillaPlus {

using Vec2 = std::array<float, 2>;
using Vec3 = std::array<float, 3>;
using Color = std::array<float, 4>;
using Triangle = std::array<std::uint32_t, 3>;

// Column order follows the Skyrim shader's bitangent, tangent, normal matrix.
struct NormalBasis {
	Vec3 bitangent, tangent, normal;
};

struct Vertex {
	Vec3 position{};
	Vec3 normal{0, 0, 1};
	Vec2 uv{};
	Color color{1, 1, 1, 1};
	bool boundary = false;
};

using Barycentric = std::array<double, 3>;
struct TriangleOrigin {
	std::uint32_t triangle = 0;
	std::array<Barycentric, 3> corners{};
};
struct SourceGeometry {
	std::vector<Vertex> vertices;
	std::vector<Triangle> triangles;
};

struct SourcePatch {
	std::vector<std::uint32_t> triangles;
};
struct UVIsland {
	std::vector<std::uint32_t> triangles;
};
struct SourceSurfaceLocation {
	std::uint32_t triangle = 0;
	Barycentric barycentric{};
};
struct PatchBoundaryEdge {
	std::uint32_t from = 0, to = 0;
	bool protectedEdge = false;
};
struct PatchBoundary {
	std::vector<std::vector<PatchBoundaryEdge>> loops;
};
struct PatchRemesh {
	SourcePatch sourcePatch;
	std::vector<Vertex> vertices;
	std::vector<Triangle> triangles;
	std::vector<SourceSurfaceLocation> sourceLocations;
	std::vector<std::array<std::uint32_t, 2>> protectedBoundaryEdges;
};
struct PatchRetopoReport {
	std::size_t sourceFaces = 0, loops = 0, boundaryEdges = 0, protectedEdges = 0;
	std::size_t generatedVertices = 0, generatedTriangles = 0;
	double maximumSourceDeviation = 0;
	bool accepted = false, fallback = true, topologyChanged = false;
	std::string reason;
};
struct PatchAuthority {
	std::shared_ptr<const SourceGeometry> original;
	std::vector<SourcePatch> patches;
	std::vector<std::uint32_t> workingFacePatch;
};
struct SourceSample {
	std::uint32_t patch = 0;
	SourceSurfaceLocation location;
};

// The future NIF adapter must reject unsupported attributes, not drop them.
// Tangents are derived data and must be rebuilt with NifSkope at commit.
struct Mesh {
	std::vector<Vertex> vertices;
	std::vector<Triangle> triangles;
	std::shared_ptr<const SourceGeometry> source;
	std::vector<TriangleOrigin> origins;
	std::shared_ptr<const PatchAuthority> patchAuthority;
};

struct Limits {
	std::size_t vertices = 65535;
	std::size_t triangles = 65535;
};

struct HeightField {
	std::size_t width = 0;
	std::size_t height = 0;
	std::vector<float> values;
};

enum class Blend { Add, Multiply, Screen, Overlay, Min, Max };
enum class Address { Clamp, Wrap };

struct Source {
	const HeightField *field = nullptr;
	float weight = 1;
	float multiplier = 1;
};

struct Conditioning {
	float black = 0;
	float white = 1;
	float midpoint = 0.5f; // Input value mapped to 0.5 after black/white normalization.
	float contrast = 1;
	float outputMin = 0;
	float outputMax = 1;
	bool invert = false;
};

struct Displacement {
	float neutral = 0.5f;
	float strength = 0;
	float positiveLimit = 1;
	float negativeLimit = 1;
	Vec2 uvScale{1, 1};
	Vec2 uvOffset{0, 0};
	Address u = Address::Wrap;
	Address v = Address::Wrap;
	bool pinBoundaries = false;
	bool preventPinching = false;
	float maxSlope = 0.5f;
	bool removeSectionBias = false;
	// Optional signed target in mesh units, sampled and clamped against the
	// original signed height request. Borrowed only for this processing call.
	const HeightField *profileTarget = nullptr;
};

enum class SurfaceProfile { Neutral, Rock, Wood, Masonry, Metal, Soil };
struct SurfaceProfileParams {
	double broadAttenuation, incoherentBroadAttenuation, blobAttenuation;
	double ridgeGuard, valleyGuard, fineGuard, fineAttenuation;
};
const SurfaceProfileParams &surfaceProfileParams(SurfaceProfile profile);
struct SurfacePrior {
	HeightField original, broad, fine, coherence, fineCoherence, anisotropy, ridge, valley, bumpBowl, suppression, target;
	double originalRms=0,targetRms=0,broadEnergyBefore=0,broadEnergyAfter=0,meanSuppression=0;
	double detailEnergyBefore=0,detailEnergyAfter=0;
	float originalMin=0,originalMax=0,targetMin=0,targetMax=0;
};
SurfacePrior analyzeSurfaceMorphology(const HeightField &height,float neutral,float strength,
	Address u=Address::Wrap,Address v=Address::Wrap);
SurfacePrior applySurfaceProfile(const SurfacePrior &morphology,SurfaceProfile profile,
	Address u=Address::Wrap,Address v=Address::Wrap);
SurfacePrior surfacePrior(const HeightField &height,float neutral,float strength,SurfaceProfile profile,
	Address u=Address::Wrap,Address v=Address::Wrap);

struct DisplacementStats {
	std::size_t limitedVertices = 0;
	float orientationScale = 1;
	std::size_t centeredSections = 0;
	double maximumRemovedBias = 0;
	double maximumResidualBias = 0;
	double requestedRms = 0;
	double appliedRms = 0;
};
struct DarkFeatureSettings {
	bool enabled = false;
	float threshold = 0.01f;
	float strength = 0.5f;
	float maximumShift = 0.10f; // Fraction of shortest incident generated edge.
};
struct DarkFeatureStats {
	bool enabled = false;
	std::size_t eligible = 0, aboveThreshold = 0, proposals = 0, accepted = 0, rejected = 0;
	std::size_t topologyRejected = 0, provenanceRejected = 0;
	std::size_t provenanceSkipped = 0;
	double averageDistance = 0, maximumDistance = 0;
};
// Depression residual of the conditioned height, in texture sampling space.
HeightField darkFeatureAttraction(const HeightField &field, float sigma, Address u, Address v);
// Counts/connectivity stay fixed; only strictly interior generated samples can
// move. Shared reference edges and all source vertices remain authoritative.
Mesh relocateDarkFeatures(const Mesh &mesh, const HeightField &attraction,
	const Displacement &mapping, const DarkFeatureSettings &settings,
	DarkFeatureStats *stats = nullptr, Limits limits = {});
struct UVIsolationAssessment {
	bool useLegacy = false;
	std::size_t islands = 0;
	std::string reason;
};

// Throw std::invalid_argument for malformed data/settings, std::length_error
// for budgets. Returned buffers own their data; input is unchanged on failure.
void validate(const Mesh &mesh, Limits limits = {});
// Capture immutable source triangles and mark open edges, including duplicated
// UV/hard seams, as pinned boundaries. Never merge by position.
Mesh captureSource(const Mesh &mesh, Limits limits = {});
// Group face-connected UV regions without welding coincident geometry. Split
// at separate indices and mirrored orientation; reject degenerate UV faces.
std::vector<UVIsland> identifyUVIslands(const Mesh &mesh, Limits limits = {});
// Separate vertices shared by different UV islands, without welding or
// changing original vertices. Newly exposed island edges are pinned; source
// correspondence continues to point to the immutable input triangles.
Mesh isolateUVIslands(const Mesh &mesh, Limits limits = {});
// Conservatively select original handling when transformed UVs tile, overlap,
// are ambiguous, or would exceed the analysis budget. Does not alter UVs.
UVIsolationAssessment assessUVIsolation(const Mesh &mesh, const Displacement &mapping, Limits limits = {});
// Conservative, coplanar, affine-attribute patches within one material shape.
// Explicit protected edges use original vertex indices. No mesh is modified.
std::vector<SourcePatch> identifySourcePatches(const Mesh &mesh,
	const std::vector<std::array<std::uint32_t, 2>> &protectedEdges = {});
// Recover oriented topological boundary loops without simplifying vertices.
// Reject inconsistent winding, incompatible/protected internal edges,
// disconnected triangles, branching/open boundaries and intersecting loops.
// Interior triangle overlaps must still be checked before parameterization.
PatchBoundary recoverPatchBoundary(const Mesh &mesh, const SourcePatch &patch,
	const std::vector<std::array<std::uint32_t, 2>> &protectedEdges = {});
// Validate the coplanar source domain, including interior triangle overlaps.
// Throws on invalid or ambiguous input; does not modify the mesh.
void validatePatchDomain(const Mesh &mesh, const SourcePatch &patch,
	const std::vector<std::array<std::uint32_t, 2>> &protectedEdges = {});
// Locate a point on a patch; false means no unique, verified surface location.
bool locateOnSourcePatch(const Mesh &mesh, const SourcePatch &patch, Vec3 point,
	SourceSurfaceLocation &location);
// Experimental four-edge coplanar patch candidate. Returns false without
// changing output if unsupported or any verification fails. The result has
// per-vertex correspondence; it is not yet connected to NIF write-back.
bool remeshFourEdgePatch(const Mesh &mesh, const SourcePatch &patch, unsigned divisions,
	PatchRemesh &output, const std::vector<std::array<std::uint32_t, 2>> &protectedEdges = {},
	std::string *rejectionReason = nullptr);
// Transactional experimental path: verify the candidate domain and source
// correspondence before replacing output; otherwise output is untouched.
PatchRetopoReport tryRetopologizePatch(const Mesh &mesh, const SourcePatch &patch, unsigned divisions,
	PatchRemesh &output, const std::vector<std::array<std::uint32_t, 2>> &protectedEdges = {});
// Independently verify a proposed lattice without modifying it.
PatchRetopoReport verifyRetopologyCandidate(const Mesh &mesh, const SourcePatch &patch, unsigned divisions,
	const PatchRemesh &candidate, const std::vector<std::array<std::uint32_t, 2>> &protectedEdges = {});
// Build an independent working reference with verified patch replacements.
// Unsupported patches retain their exact original face indices and vertices.
Mesh retopologizeCoplanarPatches(const Mesh &mesh, std::vector<PatchRetopoReport> &reports,
	const std::vector<std::array<std::uint32_t, 2>> &protectedEdges = {}, Limits limits = {});
// Resolve each used working vertex to its immutable original patch and face.
std::vector<SourceSample> resolvePatchSources(const Mesh &mesh, Limits limits = {});
void validate(const HeightField &field);
// Average the input pixel footprint for each output pixel, including partial
// pixels. This prevents undersampled texture detail from aliasing into relief.
HeightField areaResample(const HeightField &field, std::size_t width, std::size_t height);
float diffuseScalar(Color rgba);
// Experimental direction-derived relief cue, NOT reconstructed physical height.
// Input is unsigned [0,1] tangent-space normal RGB. Flat +Z maps to neutral 0.5.
float normalReliefCue(Color rgba);
// Re-express one source normal sample in the displaced surface's basis.
// Basis columns must be expressed in the same coordinate space and are
// normalized as in the Skyrim preview shader. Alpha passes through unchanged.
// Degenerate bases and invalid inputs are rejected.
Color residualNormalSample(Color source, const NormalBasis &sourceBasis, const NormalBasis &displacedBasis);
float sample(const HeightField &field, Vec2 uv, Address u, Address v);
float sampleMapped(const HeightField &field, Vec2 uv, const Displacement &mapping);
// Resample a rectangular raw-UV region, using the same transform as displacement.
HeightField sampleMappedGrid(const HeightField &field, Vec2 minimum, Vec2 maximum,
	std::size_t width, std::size_t height, const Displacement &mapping);
// Sources are blended in order. Weight interpolates between the accumulated
// value and the blend result. Disabled/missing sources are ignored; no active
// sources is an error. Different resolutions must be resampled by the adapter.
HeightField blend(const std::vector<Source> &sources, Blend mode);
HeightField condition(const HeightField &field, const Conditioning &settings);
// Pixel-space bilateral smoothing. A zero spatial sigma leaves the field unchanged.
HeightField bilateral(const HeightField &field, float spatialSigma, float rangeSigma,
	Address u = Address::Wrap, Address v = Address::Wrap);
// Retain the coarse base and weight the medium and fine frequency bands.
// Sigmas are measured in height-field pixels; coarseSigma must exceed fineSigma.
HeightField frequencyBands(const HeightField &field, float fineSigma, float coarseSigma,
	float mediumGain, float fineGain, Address u = Address::Wrap, Address v = Address::Wrap);
// Recenter local texture detail around the neutral displacement height while
// suppressing broad height bias. Sigma is measured in height-field pixels.
HeightField shallowSurfaceDetail(const HeightField &field, float sigma, float neutral,
	Address u = Address::Wrap, Address v = Address::Wrap);
std::array<std::size_t, 256> histogram(const HeightField &field);
Mesh subdivide(const Mesh &mesh, unsigned depth, Limits limits = {});
// Split where sampled height differs from the triangle's linear height
// representation, with modest gradient and fixed-preview-silhouette weighting.
// Tolerance is in conditioned height units, not world-space distance. Shared
// midpoint indices preserve conformity and distinct seams.
Mesh subdivideAdaptive(const Mesh &mesh, const HeightField &field, unsigned depth,
	float threshold, const Displacement &mapping, Limits limits = {});
Mesh displace(const Mesh &mesh, const HeightField &field,
	const Displacement &settings, Limits limits = {}, DisplacementStats *stats = nullptr);
// Measure area-weighted signed displacement against the working source,
// separately for each connected section, including after reduction.
double maximumSectionDisplacementBias(const Mesh &mesh, Limits limits = {});
// Flip only within an original triangle, with bounded surface deviation.
// Vertex attributes and positions are never modified.
Mesh improveTriangleQuality(const Mesh &mesh, std::size_t *flips = nullptr, Limits limits = {});
// Preserve authored corner normals and add a local correction for geometric
// displacement. Distinct vertex indices are never merged across hard seams.
std::vector<Vec3> shadingNormals(const Mesh &mesh, Limits limits = {});
struct ReductionStats {
	std::size_t trianglesBefore = 0, trianglesAfter = 0, skippedRegions = 0;
	float sampledError = 0; // Maximum measured distance in model-space units.
};
// Conservative preview reduction. Error is checked at finite samples on both
// surfaces in each original triangle's barycentric domain.
Mesh reduceReconstruction(const Mesh &dense, float tolerance, ReductionStats *stats = nullptr, Limits limits = {});

} // namespace VanillaPlus
#endif
