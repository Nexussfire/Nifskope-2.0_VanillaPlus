#ifndef VANILLAPLUS_SCULPT_H
#define VANILLAPLUS_SCULPT_H
#include "vanillaplus.h"

namespace VanillaPlus {
enum class SculptTool { Push, Pull, Flatten, Smooth, Restore };
struct SculptHit { std::size_t triangle=0; Vec3 position{},normal{}; double distance=0; };
struct SculptBrush {
	SculptTool tool=SculptTool::Push;
	double radius=1, strength=.1, falloff=2;
	bool smoothFalloff=false;
};

// Immutable topology/attribute authority with bounded position-only history.
class SculptSession {
public:
	explicit SculptSession(const Mesh &source);
	const Mesh &mesh() const { return current; }
	const Mesh &original() const { return baseline; }
	const std::vector<bool> &lockedVertices() const { return locked; }
	// Release automatically detected angle creases, never seam/open-boundary locks.
	void releaseCreaseLocks(bool release);
	bool pick(const Vec3 &nearPoint,const Vec3 &farPoint,SculptHit &hit) const;
	void beginStroke(const SculptHit &anchor);
	std::size_t dab(const SculptHit &hit,const SculptBrush &brush);
	bool endStroke();
	bool undo();
	bool redo();
	bool canUndo() const { return !past.empty(); }
	bool canRedo() const { return !future.empty(); }
	std::vector<Vec3> positions() const;
	bool restoreCheckpoint(const std::vector<Vec3> &positions);
private:
	using Positions=std::vector<Vec3>;
	void setPositions(const Positions &positions);
	bool safe(const Positions &positions,bool revision=false,const Positions *before=nullptr) const;
	void remember(Positions positions);
	Mesh baseline,current;
	struct ReferenceFace { std::array<double,3> normal; double length; };
	std::vector<ReferenceFace> referenceFaces;
	std::vector<std::vector<std::uint32_t>> neighbors;
	std::vector<bool> locked,permanentLocks,creaseLocks;
	Positions lockPositions;
	std::vector<Positions> past,future;
	Positions strokeStart;
	SculptHit anchor;
	bool stroking=false;
	std::size_t historyLimit=32;
};
}
#endif
