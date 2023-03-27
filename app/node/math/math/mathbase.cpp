/***

  Olive - Non-Linear Video Editor
  Copyright (C) 2022 Olive Team

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.

***/

#include "math.h"

#include <QMatrix4x4>
#include <QVector2D>

#include "common/tohex.h"
#include "node/distort/transform/transformdistortnode.h"

namespace olive {

ShaderCode MathNodeBase::GetShaderCodeInternal(const QString &shader_id, const QString& param_a_in, const QString& param_b_in) const
{
  QStringList code_id = shader_id.split('.');

  Operation op = static_cast<Operation>(code_id.at(0).toInt());
  Pairing pairing = static_cast<Pairing>(code_id.at(1).toInt());
  NodeValue::Type type_a = static_cast<NodeValue::Type>(code_id.at(2).toInt());
  NodeValue::Type type_b = static_cast<NodeValue::Type>(code_id.at(3).toInt());

  QString operation, frag, vert;

  if (pairing == kPairTextureMatrix && op == kOpMultiply) {

    // Override the operation for this operation since we multiply texture COORDS by the matrix rather than
    const QString& tex_in = (type_a == NodeValue::kTexture) ? param_a_in : param_b_in;
    const QString& mat_in = (type_a == NodeValue::kTexture) ? param_b_in : param_a_in;

    // No-op frag shader (can we return QString() instead?)
    operation = QStringLiteral("texture(%1, ove_texcoord)").arg(tex_in);

    vert = QStringLiteral("uniform mat4 %1;\n"
                          "\n"
                          "in vec4 a_position;\n"
                          "in vec2 a_texcoord;\n"
                          "\n"
                          "out vec2 ove_texcoord;\n"
                          "\n"
                          "void main() {\n"
                          "    gl_Position = %1 * a_position;\n"
                          "    ove_texcoord = a_texcoord;\n"
                          "}\n").arg(mat_in);

  } else {
    switch (op) {
    case kOpAdd:
      operation = QStringLiteral("%1 + %2");
      break;
    case kOpSubtract:
      operation = QStringLiteral("%1 - %2");
      break;
    case kOpMultiply:
      operation = QStringLiteral("%1 * %2");
      break;
    case kOpDivide:
      operation = QStringLiteral("%1 / %2");
      break;
    case kOpPower:
      if (pairing == kPairTextureNumber) {
        // The "number" in this operation has to be declared a vec4
        if (NodeValue::type_is_numeric(type_a)) {
          operation = QStringLiteral("pow(%2, vec4(%1))");
        } else {
          operation = QStringLiteral("pow(%1, vec4(%2))");
        }
      } else {
        operation = QStringLiteral("pow(%1, %2)");
      }
      break;
    }

    operation = operation.arg(GetShaderVariableCall(param_a_in, type_a),
                              GetShaderVariableCall(param_b_in, type_b));
  }

  frag = QStringLiteral("uniform %1 %3;\n"
                        "uniform %2 %4;\n"
                        "\n"
                        "in vec2 ove_texcoord;\n"
                        "out vec4 frag_color;\n"
                        "\n"
                        "void main(void) {\n"
                        "    vec4 c = %5;\n"
                        "    c.a = clamp(c.a, 0.0, 1.0);\n" // Ensure alpha is between 0.0 and 1.0
                        "    frag_color = c;\n"
                        "}\n").arg(GetShaderUniformType(type_a),
                                   GetShaderUniformType(type_b),
                                   param_a_in,
                                   param_b_in,
                                   operation);

  return ShaderCode(frag, vert);
}

QString MathNodeBase::GetShaderUniformType(const olive::NodeValue::Type &type)
{
  switch (type) {
  case NodeValue::kTexture:
    return QStringLiteral("sampler2D");
  case NodeValue::kColor:
    return QStringLiteral("vec4");
  case NodeValue::kMatrix:
    return QStringLiteral("mat4");
  default:
    return QStringLiteral("float");
  }
}

QString MathNodeBase::GetShaderVariableCall(const QString &input_id, const NodeValue::Type &type, const QString& coord_op)
{
  if (type == NodeValue::kTexture) {
    return QStringLiteral("texture(%1, ove_texcoord%2)").arg(input_id, coord_op);
  }

  return input_id;
}

QVector4D MathNodeBase::RetrieveVector(const NodeValue &val)
{
  // QVariant doesn't know that QVector*D can convert themselves so we do it here
  switch (val.type()) {
  case NodeValue::kVec2:
    return QVector4D(val.toVec2());
  case NodeValue::kVec3:
    return QVector4D(val.toVec3());
  case NodeValue::kVec4:
  default:
    return val.toVec4();
  }
}

NodeValue MathNodeBase::PushVector(olive::NodeValue::Type type, const QVector4D &vec) const
{
  switch (type) {
  case NodeValue::kVec2:
    return NodeValue(type, QVector2D(vec), this);
  case NodeValue::kVec3:
    return NodeValue(type, QVector3D(vec), this);
  case NodeValue::kVec4:
    return NodeValue(type, vec, this);
  default:
    break;
  }

  return NodeValue();
}

QString MathNodeBase::GetOperationName(Operation o)
{
  switch (o) {
  case kOpAdd: return tr("Add");
  case kOpSubtract: return tr("Subtract");
  case kOpMultiply: return tr("Multiply");
  case kOpDivide: return tr("Divide");
  case kOpPower: return tr("Power");
  }

  return QString();
}

void MathNodeBase::PerformAllOnFloatBuffer(Operation operation, float *a, float b, int start, int end)
{
  for (int j=start;j<end;j++) {
    a[j] = PerformAll(operation, a[j], b);
  }
}

#if defined(Q_PROCESSOR_X86) || defined(Q_PROCESSOR_ARM)
void MathNodeBase::PerformAllOnFloatBufferSSE(Operation operation, float *a, float b, int start, int end)
{
  int end_divisible_4 = (end / 4) * 4;

  // Load number to multiply by into buffer
  __m128 mult = _mm_load1_ps(&b);

  switch (operation) {
  case kOpAdd:
    // Loop all values
    for(int j = 0; j < end_divisible_4; j+=4) {
      _mm_storeu_ps(a + start + j, _mm_add_ps(_mm_loadu_ps(a + start + j), mult));
    }
    break;
  case kOpSubtract:
    for(int j = 0; j < end_divisible_4; j+=4) {
      _mm_storeu_ps(a + start + j, _mm_sub_ps(_mm_loadu_ps(a + start + j), mult));
    }
    break;
  case kOpMultiply:
    for(int j = 0; j < end_divisible_4; j+=4) {
      _mm_storeu_ps(a + start + j, _mm_mul_ps(_mm_loadu_ps(a + start + j), mult));
    }
    break;
  case kOpDivide:
    for(int j = 0; j < end_divisible_4; j+=4) {
      _mm_storeu_ps(a + start + j, _mm_div_ps(_mm_loadu_ps(a + start + j), mult));
    }
    break;
  case kOpPower:
    // Fallback for operations we can't support here
    end_divisible_4 = 0;
    break;
  }

  // Handle last 1-3 bytes if necessary, or all bytes if we couldn't
  // support this op on SSE
  PerformAllOnFloatBuffer(operation, a, b, end_divisible_4, end);
}
#endif

NodeValue MathNodeBase::ValueInternal(Operation operation, Pairing pairing, const QString& param_a_in, const NodeValue& val_a, const QString& param_b_in, const NodeValue& val_b, const ValueParams &p) const
{
  qDebug() << pairing;

  switch (pairing) {

  case kPairNumberNumber:
  {
    if (val_a.type() == NodeValue::kRational && val_b.type() == NodeValue::kRational && operation != kOpPower) {
      // Preserve rationals
      return NodeValue(NodeValue::kRational,
                  QVariant::fromValue(PerformAddSubMultDiv<rational, rational>(operation, val_a.toRational(), val_b.toRational())),
                  this);
    } else {
      return NodeValue(NodeValue::kFloat,
                  PerformAll<float, float>(operation, RetrieveNumber(val_a), RetrieveNumber(val_b)),
                  this);
    }
  }

  case kPairVecVec:
  {
    // We convert all vectors to QVector4D just for simplicity and exploit the fact that kVec4 is higher than kVec2 in
    // the enum to find the largest data type
    return PushVector(qMax(val_a.type(), val_b.type()),
                      PerformAddSubMultDiv<QVector4D, QVector4D>(operation, RetrieveVector(val_a), RetrieveVector(val_b)));
  }

  case kPairMatrixVec:
  {
    QMatrix4x4 matrix = (val_a.type() == NodeValue::kMatrix) ? val_a.toMatrix() : val_b.toMatrix();
    QVector4D vec = (val_a.type() == NodeValue::kMatrix) ? RetrieveVector(val_b) : RetrieveVector(val_a);

    // Only valid operation is multiply
    return PushVector(qMax(val_a.type(), val_b.type()),
                      PerformMult<QVector4D, QMatrix4x4>(operation, vec, matrix));
  }

  case kPairVecNumber:
  {
    QVector4D vec = (NodeValue::type_is_vector(val_a.type()) ? RetrieveVector(val_a) : RetrieveVector(val_b));
    float number = RetrieveNumber((val_a.type() & NodeValue::kMatrix) ? val_b : val_a);

    // Only multiply and divide are valid operations
    return PushVector(val_a.type(), PerformMultDiv<QVector4D, float>(operation, vec, number));
  }

  case kPairMatrixMatrix:
  {
    QMatrix4x4 mat_a = val_a.toMatrix();
    QMatrix4x4 mat_b = val_b.toMatrix();
    return NodeValue(NodeValue::kMatrix, PerformAddSubMult<QMatrix4x4, QMatrix4x4>(operation, mat_a, mat_b), this);
  }

  case kPairColorColor:
  {
    Color col_a = val_a.toColor();
    Color col_b = val_b.toColor();

    // Only add and subtract are valid operations
    return NodeValue(NodeValue::kColor, QVariant::fromValue(PerformAddSub<Color, Color>(operation, col_a, col_b)), this);
  }


  case kPairNumberColor:
  {
    Color col = (val_a.type() == NodeValue::kColor) ? val_a.toColor() : val_b.toColor();
    float num = (val_a.type() == NodeValue::kColor) ? val_b.toDouble() : val_a.toDouble();

    // Only multiply and divide are valid operations
    return NodeValue(NodeValue::kColor, QVariant::fromValue(PerformMult<Color, float>(operation, col, num)), this);
  }

  case kPairSampleSample:
  {
    SampleJob job(p);

    job.Insert(QStringLiteral("a"), val_a);
    job.Insert(QStringLiteral("b"), val_b);
    job.Insert(QStringLiteral("pairing"), NodeValue(NodeValue::kInt, int(pairing)));

    return NodeValue(NodeValue::kSamples, QVariant::fromValue(job), this);
  }

  case kPairTextureColor:
  case kPairTextureNumber:
  case kPairTextureTexture:
  case kPairTextureMatrix:
  {
    ShaderJob job;
    job.SetShaderID(QStringLiteral("%1.%2.%3.%4").arg(QString::number(operation),
                                                      QString::number(pairing),
                                                      QString::number(val_a.type()),
                                                      QString::number(val_b.type())));

    job.Insert(param_a_in, val_a);
    job.Insert(param_b_in, val_b);

    bool operation_is_noop = false;

    const NodeValue& number_val = val_a.type() == NodeValue::kTexture ? val_b : val_a;
    const NodeValue& texture_val = val_a.type() == NodeValue::kTexture ? val_a : val_b;
    TexturePtr texture = texture_val.toTexture();

    if (!texture) {
      operation_is_noop = true;
    } else if (pairing == kPairTextureNumber) {
      if (NumberIsNoOp(operation, RetrieveNumber(number_val))) {
        operation_is_noop = true;
      }
    } else if (pairing == kPairTextureMatrix) {
      // Only allow matrix multiplication
      const QVector2D &sequence_res = p.nonsquare_resolution();
      QVector2D texture_res(texture->params().width() * texture->pixel_aspect_ratio().toDouble(), texture->params().height());

      QMatrix4x4 adjusted_matrix = TransformDistortNode::AdjustMatrixByResolutions(number_val.toMatrix(),
                                                                                   sequence_res,
                                                                                   texture->params().offset(),
                                                                                   texture_res);

      if (operation != kOpMultiply || adjusted_matrix.isIdentity()) {
        operation_is_noop = true;
      } else {
        // Replace with adjusted matrix
        job.Insert(val_a.type() == NodeValue::kTexture ? param_b_in : param_a_in,
                        NodeValue(NodeValue::kMatrix, adjusted_matrix, this));
      }
    }

    if (operation_is_noop) {
      // Just push texture as-is
      return texture_val;
    } else {
      // Push shader job
      return NodeValue(NodeValue::kTexture, Texture::Job(p.vparams(), job), this);
    }
    break;
  }

  case kPairSampleNumber:
  {
    // Queue a sample job
    const NodeValue& number_val = val_a.type() == NodeValue::kSamples ? val_b : val_a;
    const QString& number_param = val_a.type() == NodeValue::kSamples ? param_b_in : param_a_in;

    float number = RetrieveNumber(number_val);

    const NodeValue &sample_val = val_a.type() == NodeValue::kSamples ? val_a : val_b;

    if (IsInputStatic(number_param) && NumberIsNoOp(operation, number)) {
      return sample_val;
    } else {
      SampleJob job(p);

      job.Insert(QStringLiteral("samples"), sample_val);
      job.Insert(QStringLiteral("pairing"), NodeValue(NodeValue::kInt, int(pairing)));
      job.Insert(QStringLiteral("number"), NodeValue(NodeValue::kText, val_a.type() == NodeValue::kSamples ? param_b_in : param_a_in));

      return NodeValue(NodeValue::kSamples, QVariant::fromValue(job), this);
    }
    break;
  }

  case kPairNone:
  case kPairCount:
    break;
  }

  return NodeValue();
}

void MathNodeBase::ProcessSamplesSamplesInternal(const ValueParams &p, Operation operation, const SampleBuffer &samples_a, const SampleBuffer &samples_b, SampleBuffer &mixed_samples) const
{
  size_t max_samples = qMax(samples_a.sample_count(), samples_b.sample_count());
  size_t min_samples = qMin(samples_a.sample_count(), samples_b.sample_count());

  for (int i=0;i<mixed_samples.audio_params().channel_count();i++) {
    // Mix samples that are in both buffers
    for (size_t j=0;j<min_samples;j++) {
      mixed_samples.data(i)[j] = PerformAll<float, float>(operation, samples_a.data(i)[j], samples_b.data(i)[j]);
    }
  }

  if (max_samples > min_samples) {
    size_t remainder = max_samples - min_samples;

    const SampleBuffer &larger_buffer = (max_samples == samples_a.sample_count()) ? samples_a : samples_b;

    for (int i=0;i<mixed_samples.audio_params().channel_count();i++) {
      memcpy(&mixed_samples.data(i)[min_samples],
             &larger_buffer.data(i)[min_samples],
             remainder * sizeof(float));
    }
  }
}

void MathNodeBase::ProcessSamplesNumberInternal(const ValueParams &p, MathNodeBase::Operation operation, const QString &number_in, const olive::SampleBuffer &input, olive::SampleBuffer &output) const
{
  if (IsInputStatic(number_in)) {
    auto f = GetStandardValue(number_in).toDouble();
    output = input;

    for (int i=0;i<output.audio_params().channel_count();i++) {
#if defined(Q_PROCESSOR_X86) || defined(Q_PROCESSOR_ARM)
      // Use SSE instructions for optimization
      PerformAllOnFloatBufferSSE(operation, output.data(i), f, 0, output.sample_count());
#else
      PerformAllOnFloatBuffer(operation, output.data(i), f, 0, output.sample_count());
#endif
    }
  } else {
    for (size_t j = 0; j < input.sample_count(); j++) {
      rational this_sample_time = p.time().in() + rational(j, input.audio_params().sample_rate());
      ValueParams this_sample_p(p.vparams(), p.aparams(), TimeRange(this_sample_time, this_sample_time + input.audio_params().sample_rate_as_time_base()), p.loop_mode(), p.cancel_atom());
      auto v = GetInputValue(this_sample_p, number_in).toDouble();

      for (int i=0;i<output.audio_params().channel_count();i++) {
        output.data(i)[j] = PerformAll<float, float>(operation, input.data(i)[j], v);
      }
    }
  }
}

float MathNodeBase::RetrieveNumber(const NodeValue &val)
{
  if (val.type() == NodeValue::kRational) {
    return val.toRational().toDouble();
  } else {
    return val.toDouble();
  }
}

bool MathNodeBase::NumberIsNoOp(const MathNodeBase::Operation &op, const float &number)
{
  switch (op) {
  case kOpAdd:
  case kOpSubtract:
    if (qIsNull(number)) {
      return true;
    }
    break;
  case kOpMultiply:
  case kOpDivide:
  case kOpPower:
    if (qFuzzyCompare(number, 1.0f)) {
      return true;
    }
    break;
  }

  return false;
}

MathNodeBase::PairingCalculator::PairingCalculator(const NodeValueTable &table_a, const NodeValueTable &table_b)
{
  QVector<int> pair_likelihood_a = GetPairLikelihood(table_a);
  QVector<int> pair_likelihood_b = GetPairLikelihood(table_b);

  int weight_a = qMax(0, table_b.Count() - table_a.Count());
  int weight_b = qMax(0, table_a.Count() - table_b.Count());

  QVector<int> likelihoods(kPairCount);

  for (int i=0;i<kPairCount;i++) {
    if (pair_likelihood_a.at(i) == -1 || pair_likelihood_b.at(i) == -1) {
      likelihoods.replace(i, -1);
    } else {
      likelihoods.replace(i, pair_likelihood_a.at(i) + weight_a + pair_likelihood_b.at(i) + weight_b);
    }
  }

  most_likely_pairing_ = kPairNone;

  for (int i=0;i<likelihoods.size();i++) {
    if (likelihoods.at(i) > -1) {
      if (most_likely_pairing_ == kPairNone
          || likelihoods.at(i) > likelihoods.at(most_likely_pairing_)) {
        most_likely_pairing_ = static_cast<Pairing>(i);
      }
    }
  }

  if (most_likely_pairing_ != kPairNone) {
    most_likely_value_a_ = table_a.at(pair_likelihood_a.at(most_likely_pairing_));
    most_likely_value_b_ = table_b.at(pair_likelihood_b.at(most_likely_pairing_));
  }
}

QVector<int> MathNodeBase::PairingCalculator::GetPairLikelihood(const NodeValueTable &table)
{
  QVector<int> likelihood(kPairCount, -1);

  for (int i=0;i<table.Count();i++) {
    NodeValue::Type type = table.at(i).type();

    int weight = i;

    if (NodeValue::type_is_vector(type)) {
      likelihood.replace(kPairVecVec, weight);
      likelihood.replace(kPairVecNumber, weight);
      likelihood.replace(kPairMatrixVec, weight);
    } else if (type == NodeValue::kMatrix) {
      likelihood.replace(kPairMatrixMatrix, weight);
      likelihood.replace(kPairMatrixVec, weight);
      likelihood.replace(kPairTextureMatrix, weight);
    } else if (type == NodeValue::kColor) {
      likelihood.replace(kPairColorColor, weight);
      likelihood.replace(kPairNumberColor, weight);
      likelihood.replace(kPairTextureColor, weight);
    } else if (NodeValue::type_is_numeric(type)) {
      likelihood.replace(kPairNumberNumber, weight);
      likelihood.replace(kPairVecNumber, weight);
      likelihood.replace(kPairNumberColor, weight);
      likelihood.replace(kPairTextureNumber, weight);
      likelihood.replace(kPairSampleNumber, weight);
    } else if (type == NodeValue::kSamples) {
      likelihood.replace(kPairSampleSample, weight);
      likelihood.replace(kPairSampleNumber, weight);
    } else if (type == NodeValue::kTexture) {
      likelihood.replace(kPairTextureTexture, weight);
      likelihood.replace(kPairTextureNumber, weight);
      likelihood.replace(kPairTextureColor, weight);
      likelihood.replace(kPairTextureMatrix, weight);
    }
  }

  return likelihood;
}

bool MathNodeBase::PairingCalculator::FoundMostLikelyPairing() const
{
  return (most_likely_pairing_ > kPairNone && most_likely_pairing_ < kPairCount);
}

MathNodeBase::Pairing MathNodeBase::PairingCalculator::GetMostLikelyPairing() const
{
  return most_likely_pairing_;
}

const NodeValue &MathNodeBase::PairingCalculator::GetMostLikelyValueA() const
{
  return most_likely_value_a_;
}

const NodeValue &MathNodeBase::PairingCalculator::GetMostLikelyValueB() const
{
  return most_likely_value_b_;
}

template<typename T, typename U>
T MathNodeBase::PerformAll(Operation operation, T a, U b)
{
  switch (operation) {
  case kOpAdd:
    return a + b;
  case kOpSubtract:
    return a - b;
  case kOpMultiply:
    return a * b;
  case kOpDivide:
    return a / b;
  case kOpPower:
    return std::pow(a, b);
  }

  return a;
}

template<typename T, typename U>
T MathNodeBase::PerformMultDiv(Operation operation, T a, U b)
{
  switch (operation) {
  case kOpMultiply:
    return a * b;
  case kOpDivide:
    return a / b;
  case kOpAdd:
  case kOpSubtract:
  case kOpPower:
    break;
  }

  return a;
}

template<typename T, typename U>
T MathNodeBase::PerformAddSub(Operation operation, T a, U b)
{
  switch (operation) {
  case kOpAdd:
    return a + b;
  case kOpSubtract:
    return a - b;
  case kOpMultiply:
  case kOpDivide:
  case kOpPower:
    break;
  }

  return a;
}

template<typename T, typename U>
T MathNodeBase::PerformMult(Operation operation, T a, U b)
{
  switch (operation) {
  case kOpMultiply:
    return a * b;
  case kOpAdd:
  case kOpSubtract:
  case kOpDivide:
  case kOpPower:
    break;
  }

  return a;
}

template<typename T, typename U>
T MathNodeBase::PerformAddSubMult(Operation operation, T a, U b)
{
  switch (operation) {
  case kOpAdd:
    return a + b;
  case kOpSubtract:
    return a - b;
  case kOpMultiply:
    return a * b;
  case kOpDivide:
  case kOpPower:
    break;
  }

  return a;
}

template<typename T, typename U>
T MathNodeBase::PerformAddSubMultDiv(Operation operation, T a, U b)
{
  switch (operation) {
  case kOpAdd:
    return a + b;
  case kOpSubtract:
    return a - b;
  case kOpMultiply:
    return a * b;
  case kOpDivide:
    return a / b;
  case kOpPower:
    break;
  }

  return a;
}

}
