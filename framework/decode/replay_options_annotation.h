#ifndef GFXRECON_REPLAY_OPTIONS_ANNOTATION_H
#define GFXRECON_REPLAY_OPTIONS_ANNOTATION_H

#include "decode/annotation_handler.h"
#include "format/format.h"

#include <sstream>
#include <iterator>

GFXRECON_BEGIN_NAMESPACE(gfxrecon)
GFXRECON_BEGIN_NAMESPACE(decode)

class ReplayOptionsAnnotationHandler : public gfxrecon::decode::AnnotationHandler
{
  public:
    virtual void ProcessAnnotation(uint64_t                         block_index,
                                   gfxrecon::format::AnnotationType type,
                                   const std::string&               label,
                                   const std::string&               data);
    std::string  GetReplayOptions();

  private:
    std::string replay_options_;
};

// Returns a vector of replay arguments saved in trace annotation
std::vector<std::string> GetTraceReplayOptions(const std::string& filename);

GFXRECON_END_NAMESPACE(decode)
GFXRECON_END_NAMESPACE(gfxrecon)

#endif // GFXRECON_REPLAY_OPTIONS_ANNOTATION_H
