// Copyright (c) 2023 PaddlePaddle Authors. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "paddle/fluid/pir/dialect/operator/interface/infer_symbolic_shape/binary_infer_sym.h"
#include "paddle/common/ddim.h"
#include "paddle/common/flags.h"
#include "paddle/fluid/pir/dialect/operator/interface/infer_symbolic_shape/infer_sym_utils.h"

COMMON_DECLARE_bool(manually_trans_conv_filter);

namespace {

inline void UpdatePaddingAndDilation(
    std::vector<symbol::DimExpr> *paddings,
    std::vector<symbol::DimExpr> *dilation,
    const std::string padding_algorithm,
    const std::vector<symbol::DimExpr> data_dims,
    const std::vector<int> &strides,
    const std::vector<symbol::DimExpr> &ksize) {
  // set padding size == data_dims.size() * 2
  if (paddings->size() == data_dims.size()) {
    for (size_t i = 0; i < data_dims.size(); ++i) {
      symbol::DimExpr copy_pad = *(paddings->begin() + 2 * i);
      paddings->insert(paddings->begin() + 2 * i + 1, copy_pad);
    }
  }

  // when padding_algorithm is "VALID" or "SAME"
  symbol::DimExpr zero{0};
  symbol::DimExpr one{1};
  symbol::DimExpr two{2};
  if (padding_algorithm == "SAME") {
    symbol::DimExprBuilder builder;
    for (size_t i = 0; i < data_dims.size(); ++i) {
      symbol::DimExpr out_size = (data_dims[i] + strides[i] - 1) / strides[i];
      symbol::DimExpr pad_sum = builder.Max(
          (out_size - one) * strides[i] + ksize[i] - data_dims[i], zero);

      symbol::DimExpr pad_0 = pad_sum / two;
      symbol::DimExpr pad_1 = pad_sum - pad_0;

      *(paddings->begin() + i * 2) = pad_0;
      *(paddings->begin() + i * 2 + 1) = pad_1;

      // dilation
      *(dilation->begin() + i) = one;
    }

  } else if (padding_algorithm == "VALID") {
    for (auto it = paddings->begin(); it != paddings->end(); it++) {
      *it = zero;
    }
  }
}

}  // namespace
namespace paddle::dialect {

bool AllcloseOpInferSymbolicShape(
    pir::Operation *op, pir::InferSymbolicShapeContext *infer_context) {
  const auto x_shape =
      infer_context->GetShapeOrDataForValue(op->operand_source(0)).shape();
  const auto y_shape =
      infer_context->GetShapeOrDataForValue(op->operand_source(1)).shape();
  PADDLE_ENFORCE_EQ(x_shape.size(),
                    y_shape.size(),
                    common::errors::PreconditionNotMet(
                        "Input(X) and Input(Y) must have the same "
                        "dimension size. but got %d vs %d",
                        x_shape.size(),
                        y_shape.size()));
  for (size_t i = 0; i < x_shape.size(); ++i) {
    infer_context->AddEqualCstr(x_shape[i], y_shape[i]);
  }

  infer_context->SetShapeOrDataForValue(
      op->result(0),
      symbol::ShapeOrDataDimExprs{
          symbol::TensorShapeOrDataDimExprs(std::vector<symbol::DimExpr>{})});
  return true;
}

bool BoxClipOpInferSymbolicShape(
    pir::Operation *op, pir::InferSymbolicShapeContext *infer_context) {
  const auto &input_shape =
      infer_context->GetShapeOrDataForValue(op->operand_source(0)).shape();
  const auto &im_info_shape =
      infer_context->GetShapeOrDataForValue(op->operand_source(1)).shape();

  // Check rank and dimensions of input tensors
  const auto &three = symbol::DimExpr{3};
  const auto &four = symbol::DimExpr{4};
  infer_context->AddEqualCstr(input_shape[input_shape.size() - 1], four);
  PADDLE_ENFORCE_EQ(im_info_shape.size(),
                    2,
                    common::errors::InvalidArgument(
                        "The rank of Input(im_info) in BoxClipOp must be 2. "
                        "But received rank = %d",
                        im_info_shape.size()));
  infer_context->AddEqualCstr(im_info_shape[1], three);

  infer_context->SetShapeOrDataForValue(
      op->result(0),
      symbol::ShapeOrDataDimExprs{
          symbol::TensorShapeOrDataDimExprs(input_shape)});

  return true;
}

bool Atan2OpInferSymbolicShape(pir::Operation *op,
                               pir::InferSymbolicShapeContext *infer_context) {
  const auto x_shape =
      infer_context->GetShapeOrDataForValue(op->operand_source(0)).shape();
  const auto y_shape =
      infer_context->GetShapeOrDataForValue(op->operand_source(1)).shape();
  PADDLE_ENFORCE_EQ(x_shape.size(),
                    y_shape.size(),
                    common::errors::PreconditionNotMet(
                        "Input(X) and Input(Y) must have the same "
                        "dimension size. but got %d vs %d",
                        x_shape.size(),
                        y_shape.size()));
  for (size_t i = 0; i < x_shape.size(); ++i) {
    infer_context->AddEqualCstr(x_shape[i], y_shape[i]);
  }

  infer_context->SetShapeOrDataForValue(
      op->result(0),
      symbol::ShapeOrDataDimExprs{symbol::TensorShapeOrDataDimExprs(x_shape)});
  return true;
}

bool BceLossOpInferSymbolicShape(
    pir::Operation *op, pir::InferSymbolicShapeContext *infer_context) {
  const auto &input_shape =
      infer_context->GetShapeOrDataForValue(op->operand_source(0));
  const auto &label_shape =
      infer_context->GetShapeOrDataForValue(op->operand_source(1));

  int rank = input_shape.shape().size();
  PADDLE_ENFORCE_EQ(rank,
                    label_shape.shape().size(),
                    common::errors::InvalidArgument(
                        "Input(X) and Input(Label) shall have the same rank."
                        "But received: the rank of Input(X) is [%d], "
                        "the rank of Input(Label) is [%d].",
                        rank,
                        label_shape.shape().size()));

  for (int i = 0; i < rank; ++i) {
    infer_context->AddEqualCstr(input_shape.shape()[i], label_shape.shape()[i]);
  }

  infer_context->SetShapeOrDataForValue(op->result(0), input_shape);

  return true;
}

bool BceLoss_OpInferSymbolicShape(
    pir::Operation *op, pir::InferSymbolicShapeContext *infer_context) {
  return BceLossOpInferSymbolicShape(op, infer_context);
}

bool BinomialOpInferSymbolicShape(
    pir::Operation *op, pir::InferSymbolicShapeContext *infer_context) {
  const auto &count_shape =
      infer_context->GetShapeOrDataForValue(op->operand_source(0)).shape();
  const auto &prob_shape =
      infer_context->GetShapeOrDataForValue(op->operand_source(1)).shape();
  PADDLE_ENFORCE_EQ(count_shape.size(),
                    prob_shape.size(),
                    common::errors::PreconditionNotMet(
                        "Input(count) and Input(prob) must have the same "
                        "dimension size. but got %d vs %d",
                        count_shape.size(),
                        prob_shape.size()));
  for (size_t i = 0; i < count_shape.size(); ++i) {
    infer_context->AddEqualCstr(count_shape[i], prob_shape[i]);
  }
  infer_context->SetShapeOrDataForValue(
      op->result(0),
      symbol::ShapeOrDataDimExprs{
          symbol::TensorShapeOrDataDimExprs(count_shape)});
  return true;
}

bool Binomial_OpInferSymbolicShape(
    pir::Operation *op, pir::InferSymbolicShapeContext *infer_context) {
  return BinomialOpInferSymbolicShape(op, infer_context);
}

// bool BincountOpInferSymbolicShape(pir::Operation *op,
//                                   pir::InferSymbolicShapeContext
//                                   *infer_context) {
//   // pass
//   return true;
// }

// bool BmmOpInferSymbolicShape(pir::Operation *op,
//                              pir::InferSymbolicShapeContext *infer_context) {
//   // pass
//   return true;
// }

// bool CholeskySolveOpInferSymbolicShape(pir::Operation *op,
//                                        pir::InferSymbolicShapeContext
//                                        *infer_context) {
//   // pass
//   return true;
// }

bool CtcAlignOpInferSymbolicShape(
    pir::Operation *op, pir::InferSymbolicShapeContext *infer_context) {
  const auto &input_shape =
      infer_context->GetShapeOrDataForValue(op->operand_source(0));
  std::vector<symbol::DimExpr> out_shape = {input_shape.shape()[0], 1};
  symbol::ShapeOrDataDimExprs shape_data{
      symbol::TensorShapeOrDataDimExprs(out_shape)};
  infer_context->SetShapeOrDataForValue(op->result(0), input_shape);
  infer_context->SetShapeOrDataForValue(op->result(1), shape_data);
  return true;
}

bool Conv2dOpInferSymbolicShape(pir::Operation *op,
                                pir::InferSymbolicShapeContext *infer_context) {
  const std::vector<int> strides =
      paddle::dialect::details::GetVectorAttr<int>(op, "strides");

  std::vector<int> paddings =
      paddle::dialect::details::GetVectorAttr<int>(op, "paddings");

  std::vector<int> dilations =
      paddle::dialect::details::GetVectorAttr<int>(op, "dilations");

  const auto &attributes = op->attributes();
  const std::string data_format =
      attributes.at("data_format").dyn_cast<pir::StrAttribute>().AsString();

  const std::string padding_algorithm = attributes.at("padding_algorithm")
                                            .dyn_cast<pir::StrAttribute>()
                                            .AsString();

  const auto in_s_or_d =
      infer_context->GetShapeOrDataForValue(op->operand_source(0));
  const auto filter_s_or_d =
      infer_context->GetShapeOrDataForValue(op->operand_source(1));

  const bool channel_last = (data_format == "NHWC" || data_format == "NDHWC");

  const std::vector<symbol::DimExpr> in_data_dims =
      channel_last ? std::vector<symbol::DimExpr>(in_s_or_d.shape().begin() + 1,
                                                  in_s_or_d.shape().end() - 1)
                   : std::vector<symbol::DimExpr>(in_s_or_d.shape().begin() + 2,
                                                  in_s_or_d.shape().end());

  const std::vector<symbol::DimExpr> filter_data_dims = [&]() {
    if (channel_last && FLAGS_manually_trans_conv_filter) {  // NHWC
      return std::vector<symbol::DimExpr>(filter_s_or_d.shape().begin() + 1,
                                          filter_s_or_d.shape().end() - 1);
    } else {
      return std::vector<symbol::DimExpr>(filter_s_or_d.shape().begin() + 2,
                                          filter_s_or_d.shape().end());
    }
  }();

  std::vector<symbol::DimExpr> ksize = filter_data_dims;

  std::vector<symbol::DimExpr> new_paddings;
  for (const auto &i : paddings) {
    new_paddings.push_back(symbol::DimExpr{i});
  }
  std::vector<symbol::DimExpr> new_dilations;
  for (const auto &i : dilations) {
    new_dilations.push_back(symbol::DimExpr{i});
  }

  UpdatePaddingAndDilation(&new_paddings,
                           &new_dilations,
                           padding_algorithm,
                           in_data_dims,
                           strides,
                           ksize);

  const symbol::ShapeOrDataDimExprs &shape_data = [&] {
    std::vector<symbol::DimExpr> out_s_or_d({in_s_or_d.shape()[0]});
    if (!channel_last) {
      out_s_or_d.push_back(filter_s_or_d.shape()[0]);
    }

    for (size_t i = 0; i < in_data_dims.size(); ++i) {
      const symbol::DimExpr dkernel =
          new_dilations[i] * (filter_data_dims[i] - 1) + 1;
      symbol::DimExpr output_size = (in_data_dims[i] + new_paddings[2 * i] +
                                     new_paddings[2 * i + 1] - dkernel) /
                                        strides[i] +
                                    1;
      out_s_or_d.push_back(output_size);
    }
    if (channel_last) {
      out_s_or_d.push_back(filter_s_or_d.shape()[0]);
    }

    return symbol::ShapeOrDataDimExprs{
        symbol::TensorShapeOrDataDimExprs(out_s_or_d)};
  }();

  infer_context->SetShapeOrDataForValue(op->result(0), shape_data);

  return true;
}

bool Conv3dOpInferSymbolicShape(pir::Operation *op,
                                pir::InferSymbolicShapeContext *infer_context) {
  return Conv2dOpInferSymbolicShape(op, infer_context);
}

bool CrossOpInferSymbolicShape(pir::Operation *op,
                               pir::InferSymbolicShapeContext *infer_context) {
  const auto &x_shape =
      infer_context->GetShapeOrDataForValue(op->operand_source(0));
  const auto &y_shape =
      infer_context->GetShapeOrDataForValue(op->operand_source(1));

  size_t x_dim = x_shape.shape().size();
  size_t y_dim = y_shape.shape().size();

  PADDLE_ENFORCE_EQ(x_dim,
                    y_dim,
                    common::errors::InvalidArgument(
                        "The 'shape' of Input(X) should be equal to "
                        "the 'shape' of Input(Y). But received "
                        "Input(X).dimensions = [%d], "
                        "Input(Y).dimensions = [%d]",
                        x_dim,
                        y_dim));

  for (size_t i = 0; i < x_dim; i++) {
    infer_context->AddEqualCstr(x_shape.shape()[i], y_shape.shape()[i]);
  }

  const int axis = op->attribute<pir::Int32Attribute>("axis").data();
  if (axis != common::DDim::kMaxRank) {
    const int dim = axis < 0 ? axis + x_dim : axis;
    infer_context->AddEqualCstr(x_shape.shape()[dim], symbol::DimExpr{3});
    infer_context->AddEqualCstr(y_shape.shape()[dim], symbol::DimExpr{3});
  }

  infer_context->SetShapeOrDataForValue(op->result(0), x_shape);

  return true;
}

// bool DotOpInferSymbolicShape(pir::Operation *op,
//                              pir::InferSymbolicShapeContext *infer_context) {
//   // pass
//   return true;
// }

bool EmbeddingOpInferSymbolicShape(
    pir::Operation *op, pir::InferSymbolicShapeContext *infer_context) {
  const std::vector<symbol::DimExpr> &x_dims =
      infer_context->GetShapeOrDataForValue(op->operand_source(0)).shape();
  const std::vector<symbol::DimExpr> &weight_dims =
      infer_context->GetShapeOrDataForValue(op->operand_source(1)).shape();

  const symbol::ShapeOrDataDimExprs &shape_data = [&] {
    std::vector<symbol::DimExpr> out_dims = x_dims;
    // no need to check validation of weight_dims index, since all checks have
    // been done at corresponding InferMeta
    out_dims.emplace_back(weight_dims[1]);
    return symbol::ShapeOrDataDimExprs{
        symbol::TensorShapeOrDataDimExprs(out_dims)};
  }();

  infer_context->SetShapeOrDataForValue(op->result(0), shape_data);

  return true;
}

bool EqualAllOpInferSymbolicShape(
    pir::Operation *op, pir::InferSymbolicShapeContext *infer_context) {
  const auto &x_dims =
      infer_context->GetShapeOrDataForValue(op->operand_source(0)).shape();
  const auto &y_dims =
      infer_context->GetShapeOrDataForValue(op->operand_source(1)).shape();

  PADDLE_ENFORCE_GE(
      x_dims.size(),
      y_dims.size(),
      common::errors::InvalidArgument(
          "The size of dim_y should not be greater than dim_x's."));

  std::vector<symbol::DimExpr> out_dims =
      {};  // Adjust the dimensions as necessary
  infer_context->SetShapeOrDataForValue(
      op->result(0),
      symbol::ShapeOrDataDimExprs{symbol::TensorShapeOrDataDimExprs(out_dims)});

  return true;
}

bool SparseWeightEmbeddingOpInferSymbolicShape(
    pir::Operation *op, pir::InferSymbolicShapeContext *infer_context) {
  PADDLE_THROW(common::errors::Unimplemented(
      op->name() + " 's InferSymbolicShape interface is NOT implemented now."));
  return true;
}

bool ExpandAsOpInferSymbolicShape(
    pir::Operation *op, pir::InferSymbolicShapeContext *infer_context) {
  std::vector<int> target_shape =
      paddle::dialect::details::GetVectorAttr<int>(op, "target_shape");
  const std::vector<symbol::DimExpr> &output_dims = [&] {
    const auto &input_shape_or_data =
        infer_context->GetShapeOrDataForValue(op->operand_source(1));
    if (!input_shape_or_data.isa<symbol::NullShapeOrDataDimExpr>()) {
      return input_shape_or_data.shape();
    }
    std::vector<symbol::DimExpr> output_dims;
    output_dims.reserve(target_shape.size());
    for (int shape : target_shape) {
      output_dims.push_back(shape);
    }
    return output_dims;
  }();

  infer_context->SetShapeOrDataForValue(
      op->result(0), symbol::TensorShapeOrDataDimExprs(output_dims));

  return true;
}

bool FillDiagonalTensorOpInferSymbolicShape(
    pir::Operation *op, pir::InferSymbolicShapeContext *infer_context) {
  const auto &x_shape_or_data =
      infer_context->GetShapeOrDataForValue(op->operand_source(0));
  std::vector<symbol::DimExpr> x_dims = x_shape_or_data.shape();

  infer_context->SetShapeOrDataForValue(
      op->result(0),
      symbol::ShapeOrDataDimExprs{symbol::TensorShapeOrDataDimExprs(x_dims)});

  return true;
}

bool FillDiagonalTensor_OpInferSymbolicShape(
    pir::Operation *op, pir::InferSymbolicShapeContext *infer_context) {
  return FillDiagonalTensorOpInferSymbolicShape(op, infer_context);
}

// bool FusedSoftmaxMaskOpInferSymbolicShape(pir::Operation *op,
//                                           pir::InferSymbolicShapeContext
//                                           *infer_context) {
//   // pass
//   return true;
// }

// bool GridSampleOpInferSymbolicShape(pir::Operation *op,
//                                     pir::InferSymbolicShapeContext
//                                     *infer_context) {
//   // pass
//   return true;
// }

bool GatherOpInferSymbolicShape(pir::Operation *op,
                                pir::InferSymbolicShapeContext *infer_context) {
  const auto &input_shape_or_data =
      infer_context->GetShapeOrDataForValue(op->operand_source(0));
  const auto &index_shape_or_data =
      infer_context->GetShapeOrDataForValue(op->operand_source(1));

  const auto &numel = [&] {
    symbol::DimExpr numel{1};
    for (const auto &dim_expr : index_shape_or_data.shape()) {
      numel = numel * dim_expr;
    }
    return numel;
  }();

  int axis = 0;
  const auto &attributes = op->attributes();
  if (op->HasAttribute("axis")) {  // CINN Dialect
    axis = attributes.at("axis").dyn_cast<pir::Int32Attribute>().data();
  } else {
    PADDLE_ENFORCE_EQ(
        op->num_operands() == 3,
        true,
        common::errors::InvalidArgument(
            "in GatherOpInferSymbolicShape: The number of operands should be "
            "3 when the axis is not set."));
    const auto &axis_shape_or_data =
        infer_context->GetShapeOrDataForValue(op->operand_source(2));
    axis =
        static_cast<int>(axis_shape_or_data.data().value()[0].Get<int64_t>());
  }

  const std::vector<symbol::DimExpr> &input_sym_shape =
      input_shape_or_data.data().has_value()
          ? input_shape_or_data.data().value()
          : input_shape_or_data.shape();

  const std::vector<symbol::DimExpr> &index_sym_shape =
      index_shape_or_data.data().has_value()
          ? index_shape_or_data.data().value()
          : index_shape_or_data.shape();

  if (axis < 0) axis += input_sym_shape.size();

  const auto &out_sym_shape = [&] {
    std::vector<symbol::DimExpr> out_sym_shape;

    if (index_sym_shape.size() == 0) {
      if (input_sym_shape.size() == 1) {
        out_sym_shape.push_back(symbol::DimExpr{0});
      } else {
        for (int i = 0; i < axis; ++i) {
          out_sym_shape.push_back(input_sym_shape[i]);
        }
        for (size_t i = axis + 1; i < input_sym_shape.size(); ++i) {
          out_sym_shape.push_back(input_sym_shape[i]);
        }
      }
    } else {
      for (int i = 0; i < axis; ++i) {
        out_sym_shape.push_back(input_sym_shape[i]);
      }
      out_sym_shape.push_back(numel);
      for (size_t i = axis + 1; i < input_sym_shape.size(); ++i) {
        out_sym_shape.push_back(input_sym_shape[i]);
      }
    }
    return out_sym_shape;
  }();

  symbol::ShapeOrDataDimExprs shape_data{
      symbol::TensorShapeOrDataDimExprs(out_sym_shape)};

  pir::Value res = op->result(0);
  infer_context->SetShapeOrDataForValue(res, shape_data);

  return true;
}

bool GatherNdOpInferSymbolicShape(
    pir::Operation *op, pir::InferSymbolicShapeContext *infer_context) {
  const auto &x_shape_or_data =
      infer_context->GetShapeOrDataForValue(op->operand_source(0));
  const auto &index_shape_or_data =
      infer_context->GetShapeOrDataForValue(op->operand_source(1));

  const std::vector<symbol::DimExpr> &x_sym_shape = x_shape_or_data.shape();
  const std::vector<symbol::DimExpr> &index_sym_shape =
      index_shape_or_data.shape();

  int x_dims_size = x_sym_shape.size();
  int index_dims_size = index_sym_shape.size();

  std::vector<symbol::DimExpr> result_sym_dims;
  // The result dims is
  //   Index.shape[:-1] + X.shape[Index.shape[-1]:]
  for (int i = 0; i < index_dims_size - 1; ++i) {
    result_sym_dims.emplace_back(index_sym_shape[i]);
  }

  PADDLE_ENFORCE_EQ(
      index_sym_shape[index_dims_size - 1].Has<std::int64_t>(),
      true,
      common::errors::InvalidArgument(
          "in GatherNdOpInferSymbolicShape: index[-1] should be unknown"));

  for (int i = static_cast<int>(
           index_sym_shape[index_dims_size - 1].Get<std::int64_t>());
       i < x_dims_size;
       ++i) {
    result_sym_dims.emplace_back(x_sym_shape[i]);
  }

  symbol::ShapeOrDataDimExprs shape_data{
      symbol::TensorShapeOrDataDimExprs(result_sym_dims)};

  pir::Value res = op->result(0);
  infer_context->SetShapeOrDataForValue(res, shape_data);

  return true;
}

bool HuberLossOpInferSymbolicShape(
    pir::Operation *op, pir::InferSymbolicShapeContext *infer_context) {
  const auto &input =
      infer_context->GetShapeOrDataForValue(op->operand_source(0));
  const auto &label =
      infer_context->GetShapeOrDataForValue(op->operand_source(1));
  const std::vector<symbol::DimExpr> &input_dims = input.shape();
  const std::vector<symbol::DimExpr> &label_dims = label.shape();
  PADDLE_ENFORCE_EQ(input_dims.size(),
                    label_dims.size(),
                    phi::errors::InvalidArgument(
                        "Input(input) rank and Input(label) rank should be "
                        "same, but received input rank(%d) != label rank(%d)",
                        input_dims.size(),
                        label_dims.size()));

  int rank = input_dims.size();
  for (int i = 0; i < rank; ++i) {
    infer_context->AddEqualCstr(input_dims[i], label_dims[i]);
  }

  infer_context->SetShapeOrDataForValue(
      op->result(1),
      symbol::ShapeOrDataDimExprs{
          symbol::TensorShapeOrDataDimExprs(label_dims)});
  infer_context->SetShapeOrDataForValue(
      op->result(0),
      symbol::ShapeOrDataDimExprs{
          symbol::TensorShapeOrDataDimExprs(label_dims)});

  return true;
}

// bool HistogramOpInferSymbolicShape(pir::Operation *op,
//                                    pir::InferSymbolicShapeContext
//                                    *infer_context) {
//   // pass
//   return true;
// }

bool IndexSampleOpInferSymbolicShape(
    pir::Operation *op, pir::InferSymbolicShapeContext *infer_context) {
  const symbol::ShapeOrDataDimExprs &operand_shape_or_data =
      infer_context->GetShapeOrDataForValue(op->operand_source(1));
  infer_context->SetShapeOrDataForValue(op->result(0), operand_shape_or_data);
  return true;
}

bool KldivLossOpInferSymbolicShape(
    pir::Operation *op, pir::InferSymbolicShapeContext *infer_context) {
  const auto &x_shape_or_data =
      infer_context->GetShapeOrDataForValue(op->operand_source(0));
  const auto &label_shape_or_data =
      infer_context->GetShapeOrDataForValue(op->operand_source(1));
  const auto &x_shape = x_shape_or_data.shape();
  const auto &label_shape = label_shape_or_data.shape();

  PADDLE_ENFORCE_EQ(x_shape.size(),
                    label_shape.size(),
                    common::errors::InvalidArgument(
                        "Input(X) rank and Input(Target) rank should be same, "
                        "but received X rank(%d) != Target rank(%d)",
                        x_shape.size(),
                        label_shape.size()));

  for (size_t i = 0; i < x_shape.size(); ++i) {
    infer_context->AddEqualCstr(x_shape[i], label_shape[i]);
  }

  std::string reduction =
      op->attribute<pir::StrAttribute>("reduction").AsString();
  bool reduction_valid = (reduction == "mean" || reduction == "sum" ||
                          reduction == "batchmean" || reduction == "none");
  PADDLE_ENFORCE_EQ(
      reduction_valid,
      true,
      common::errors::InvalidArgument(
          "Attr(reduction) can only be 'none'|'batchmean'|'sum'|'mean'."));

  std::vector<symbol::DimExpr> out_shape;
  if (reduction == "none") {
    out_shape = x_shape;
  } else {
    out_shape = std::vector<symbol::DimExpr>{};
  }
  infer_context->SetShapeOrDataForValue(
      op->result(0),
      symbol::ShapeOrDataDimExprs{
          symbol::TensorShapeOrDataDimExprs(out_shape)});

  return true;
}

bool KronOpInferSymbolicShape(pir::Operation *op,
                              pir::InferSymbolicShapeContext *infer_context) {
  const auto &x_shape_or_data =
      infer_context->GetShapeOrDataForValue(op->operand_source(0)).shape();
  const auto &y_shape_or_data =
      infer_context->GetShapeOrDataForValue(op->operand_source(1)).shape();
  const int rank_x = x_shape_or_data.size();
  const int rank_y = y_shape_or_data.size();
  const int rank = (rank_x > rank_y) ? rank_x : rank_y;

  std::vector<symbol::DimExpr> dim_out;
  dim_out.reserve(rank);
  const auto one = symbol::DimExpr{1};
  const auto minus_one = symbol::DimExpr{-1};
  for (int i = 0; i < rank; i++) {
    symbol::DimExpr dim_xi =
        (i < rank - rank_x) ? one : x_shape_or_data.at(i - (rank - rank_x));
    symbol::DimExpr dim_yi =
        (i < rank - rank_y) ? one : y_shape_or_data.at(i - (rank - rank_y));
    dim_out.push_back(dim_xi * dim_yi);
  }
  symbol::ShapeOrDataDimExprs shape_data{
      symbol::TensorShapeOrDataDimExprs(dim_out)};
  pir::Value res = op->result(0);
  infer_context->SetShapeOrDataForValue(res, shape_data);
  return true;
}

// bool LstsqOpInferSymbolicShape(pir::Operation *op,
//                                pir::InferSymbolicShapeContext *infer_context)
//                                {
//   // pass
//   return true;
// }

// bool MatrixRankTolOpInferSymbolicShape(pir::Operation *op,
//                                        pir::InferSymbolicShapeContext
//                                        *infer_context) {
//   // pass
//   return true;
// }

bool MaskedSelectOpInferSymbolicShape(
    pir::Operation *op, pir::InferSymbolicShapeContext *infer_context) {
  const std::vector<symbol::DimExpr> &out_dims = [&] {
    std::vector<symbol::DimExpr> out_dims;
    symbol::DimExpr out_shape =
        infer_context->GetNextSymName();  // unknown until runtime
    out_dims.push_back(out_shape);
    return out_dims;
  }();
  // Add constrains between the shapes of x and mask
  const std::vector<symbol::DimExpr> &x_shape =
      infer_context->GetShapeOrDataForValue(op->operand_source(0)).shape();
  const std::vector<symbol::DimExpr> &mask_shape =
      infer_context->GetShapeOrDataForValue(op->operand_source(1)).shape();
  size_t ndims_x = x_shape.size();
  size_t ndims_mask = mask_shape.size();
  if (ndims_x >= ndims_mask) {
    size_t diff = ndims_x - ndims_mask;
    for (size_t i = 0; i < ndims_mask; i++) {
      infer_context->AddBroadcastableCstr(x_shape[i + diff], mask_shape[i]);
    }
  } else {
    size_t diff = ndims_mask - ndims_x;
    for (size_t i = 0; i < ndims_x; i++) {
      infer_context->AddBroadcastableCstr(x_shape[i], mask_shape[i + diff]);
    }
  }

  infer_context->SetShapeOrDataForValue(
      op->result(0), symbol::TensorShapeOrDataDimExprs{out_dims});
  return true;
}

bool MatmulOpInferSymbolicShape(pir::Operation *op,
                                pir::InferSymbolicShapeContext *infer_context) {
  // x_dims can't be const or ref here, in case to be broadcasted
  std::vector<symbol::DimExpr> x_dims = [&] {
    std::vector<symbol::DimExpr> dims;
    const auto &x_shape_or_data =
        infer_context->GetShapeOrDataForValue(op->operand_source(0));
    dims = x_shape_or_data.shape();
    return dims;
  }();

  // y_dims can't be const or ref here, in case to be broadcasted
  std::vector<symbol::DimExpr> y_dims = [&] {
    std::vector<symbol::DimExpr> dims;
    const auto y_shape_or_data =
        infer_context->GetShapeOrDataForValue(op->operand_source(1));
    dims = y_shape_or_data.shape();
    return dims;
  }();

  size_t ndims_x = x_dims.size();
  size_t ndims_y = y_dims.size();

  const bool x_broadcasted = [&] {
    bool broadcasted = false;
    if (ndims_x == 1) {
      x_dims.insert(x_dims.begin(), 1);
      ndims_x = 2;
      broadcasted = true;
    }
    return broadcasted;
  }();

  const bool y_broadcasted = [&] {
    bool broadcasted = false;
    if (ndims_y == 1) {
      y_dims.emplace_back(1);
      ndims_y = 2;
      broadcasted = true;
    }
    return broadcasted;
  }();

  std::vector<symbol::DimExpr> out_dims;
  if (ndims_x > ndims_y) {
    out_dims.assign(x_dims.begin(), x_dims.end() - 2);
  } else if (ndims_x < ndims_y) {
    out_dims.assign(y_dims.begin(), y_dims.end() - 2);
  } else {
    symbol::DimExprBuilder builder;
    for (size_t i = 0; i < ndims_x - 2; ++i) {
      out_dims.emplace_back(builder.Broadcast(x_dims[i], y_dims[i]));
      infer_context->AddBroadcastableCstr(x_dims[i], y_dims[i]);
    }
  }

  bool transpose_x_attr = GetBoolAttr(op, "transpose_x");
  bool transpose_y_attr = GetBoolAttr(op, "transpose_y");
  symbol::DimExpr out_M =
      transpose_x_attr ? x_dims[ndims_x - 1] : x_dims[ndims_x - 2];
  symbol::DimExpr out_N =
      transpose_y_attr ? y_dims[ndims_y - 2] : y_dims[ndims_y - 1];
  if (!x_broadcasted) {
    out_dims.emplace_back(out_M);
  }
  if (!y_broadcasted) {
    out_dims.emplace_back(out_N);
  }

  infer_context->SetShapeOrDataForValue(op->result(0),
                                        ShapeOrData{TensorExprs(out_dims)});

  if ((ndims_x == ndims_y) && ndims_x >= 2) {
    if (transpose_x_attr == false && transpose_y_attr == false) {
      infer_context->AddEqualCstr(x_dims[ndims_x - 1], y_dims[ndims_x - 2]);
    } else if (transpose_x_attr == false && transpose_y_attr == true) {
      infer_context->AddEqualCstr(x_dims[ndims_x - 1], y_dims[ndims_x - 1]);
    } else if (transpose_x_attr == true && transpose_y_attr == false) {
      infer_context->AddEqualCstr(x_dims[ndims_x - 2], y_dims[ndims_x - 2]);
    } else {
      infer_context->AddEqualCstr(x_dims[ndims_x - 2], y_dims[ndims_x - 1]);
    }

    for (size_t i = 0; i < ndims_x - 2; ++i) {
      infer_context->AddEqualCstr(x_dims[i], y_dims[i]);
    }
  }
  return true;
}

bool MarginCrossEntropyOpInferSymbolicShape(
    pir::Operation *op, pir::InferSymbolicShapeContext *infer_context) {
  const auto &logits_shape_or_data =
      infer_context->GetShapeOrDataForValue(op->operand_source(0));
  const auto &labels_shape_or_data =
      infer_context->GetShapeOrDataForValue(op->operand_source(1));

  std::vector<symbol::DimExpr> logits_dims = logits_shape_or_data.shape();
  std::vector<symbol::DimExpr> labels_dims = labels_shape_or_data.shape();

  size_t logits_rank = logits_dims.size();
  auto axis = logits_rank - 1;

  for (size_t i = 0; i < logits_rank; i++) {
    if (i != axis) {
      infer_context->AddEqualCstr(logits_dims[i], labels_dims[i]);
    }
  }

  const auto &one = symbol::DimExpr{1};

  if (labels_dims.size() > 1) {
    infer_context->AddEqualCstr(labels_dims[axis], one);
  }

  infer_context->SetShapeOrDataForValue(
      op->result(0),
      symbol::ShapeOrDataDimExprs{
          symbol::TensorShapeOrDataDimExprs(logits_dims)});

  logits_dims[axis] = symbol::DimExpr(1);

  infer_context->SetShapeOrDataForValue(
      op->result(1),
      symbol::ShapeOrDataDimExprs{
          symbol::TensorShapeOrDataDimExprs(logits_dims)});

  return true;
}

bool MvOpInferSymbolicShape(pir::Operation *op,
                            pir::InferSymbolicShapeContext *infer_context) {
  const auto &x_shape_or_data =
      infer_context->GetShapeOrDataForValue(op->operand_source(0));
  const auto &vec_shape_or_data =
      infer_context->GetShapeOrDataForValue(op->operand_source(1));
  PADDLE_ENFORCE_EQ(x_shape_or_data.shape().size(),
                    2,
                    common::errors::InvalidArgument(
                        "The rank of input X should be 2, but is %d",
                        x_shape_or_data.shape().size()));
  PADDLE_ENFORCE_EQ(vec_shape_or_data.shape().size(),
                    1,
                    common::errors::InvalidArgument(
                        "The rank of input Vec should be 1, but is %d",
                        vec_shape_or_data.shape().size()));
  infer_context->AddEqualCstr(x_shape_or_data.shape()[1],
                              vec_shape_or_data.shape()[0]);

  std::vector<symbol::DimExpr> out_shape = {x_shape_or_data.shape()[0]};
  infer_context->SetShapeOrDataForValue(
      op->result(0),
      symbol::ShapeOrDataDimExprs{
          symbol::TensorShapeOrDataDimExprs(out_shape)});
  return true;
}

// bool PullBoxSparseOpInferSymbolicShape(pir::Operation *op,
//                                        pir::InferSymbolicShapeContext
//                                        *infer_context) {
//   // pass
//   return true;
// }

// bool PullGpuPsSparseOpInferSymbolicShape(pir::Operation *op,
//                                          pir::InferSymbolicShapeContext
//                                          *infer_context) {
//   // pass
//   return true;
// }

// bool PullSparseV2OpInferSymbolicShape(pir::Operation *op,
//                                       pir::InferSymbolicShapeContext
//                                       *infer_context) {
//   // pass
//   return true;
// }

bool SearchsortedOpInferSymbolicShape(
    pir::Operation *op, pir::InferSymbolicShapeContext *infer_context) {
  // The shape of output is the same as input `values` (op->operand_source(1))
  const symbol::ShapeOrDataDimExprs &operand_shape_or_data =
      infer_context->GetShapeOrDataForValue(op->operand_source(1));
  // TODO(fty1777): Add constrains between the shapes of `sorted_sequence` and
  // `values`
  infer_context->SetShapeOrDataForValue(op->result(0), operand_shape_or_data);
  return true;
}

bool SegmentPoolOpInferSymbolicShape(
    pir::Operation *op, pir::InferSymbolicShapeContext *infer_context) {
  const auto &input_shape_or_data =
      infer_context->GetShapeOrDataForValue(op->operand_source(0));
  const std::vector<symbol::DimExpr> &input_shape = input_shape_or_data.shape();
  const auto &ids_shape_or_data =
      infer_context->GetShapeOrDataForValue(op->operand_source(1));
  const std::vector<symbol::DimExpr> &ids_shape = ids_shape_or_data.shape();
  const std::string pool_type =
      op->attribute<pir::StrAttribute>("pooltype").AsString();

  std::vector<symbol::DimExpr> out_shape;
  if (ids_shape_or_data.data().has_value()) {
    const auto &ids_data = ids_shape_or_data.data();
    out_shape.push_back(ids_data.value()[ids_shape.size() - 1] +
                        symbol::DimExpr{1});
  } else {
    symbol::DimExpr out_unknown =
        infer_context->GetNextSymName();  // unknown until runtime
    out_shape.push_back(out_unknown);
  }
  int axis = input_shape.size();
  for (int i = 1; i < axis; ++i) {
    out_shape.push_back(input_shape[i]);
  }
  symbol::ShapeOrDataDimExprs shape_data{
      symbol::TensorShapeOrDataDimExprs(out_shape)};
  infer_context->SetShapeOrDataForValue(op->result(0), shape_data);
  if (pool_type == "MEAN") {
    std::vector<symbol::DimExpr> summed_shape;
    summed_shape.push_back(out_shape[0]);  // same as before
    summed_shape.push_back(symbol::DimExpr{1});
    infer_context->SetShapeOrDataForValue(
        op->result(1),
        symbol::ShapeOrDataDimExprs{
            symbol::TensorShapeOrDataDimExprs(summed_shape)});
  }
  return true;
}

// bool SequenceMaskOpInferSymbolicShape(pir::Operation *op,
//                                       pir::InferSymbolicShapeContext
//                                       *infer_context) {
//   // pass
//   return true;
// }

bool SwigluOpInferSymbolicShape(pir::Operation *op,
                                pir::InferSymbolicShapeContext *infer_context) {
  const auto &x_shape_or_data =
      infer_context->GetShapeOrDataForValue(op->operand_source(0));
  size_t rank = x_shape_or_data.shape().size();
  if (op->operand_source(1)) {
    const auto &y_shape_or_data =
        infer_context->GetShapeOrDataForValue(op->operand_source(1));
    for (size_t i = 0; i < rank; ++i) {
      infer_context->AddEqualCstr(x_shape_or_data.shape()[i],
                                  y_shape_or_data.shape()[i]);
    }
    infer_context->SetShapeOrDataForValue(op->result(0), x_shape_or_data);
  } else {
    std::vector<symbol::DimExpr> x_shape = x_shape_or_data.shape();
    // TODO(CINN): Add distribute constraint
    x_shape[rank - 1] = x_shape[rank - 1] / symbol::DimExpr{2};
    infer_context->SetShapeOrDataForValue(
        op->result(0),
        symbol::ShapeOrDataDimExprs{
            symbol::TensorShapeOrDataDimExprs(x_shape)});
  }
  return true;
}

bool IscloseOpInferSymbolicShape(
    pir::Operation *op, pir::InferSymbolicShapeContext *infer_context) {
  // The shape of output is the same as input `values` (op->operand_source(1))
  const symbol::ShapeOrDataDimExprs &operand_shape_or_data =
      infer_context->GetShapeOrDataForValue(op->operand_source(1));
  infer_context->SetShapeOrDataForValue(op->result(0), operand_shape_or_data);
  return true;
}

bool IndexSelectStridedOpInferSymbolicShape(
    pir::Operation *op, pir::InferSymbolicShapeContext *infer_context) {
  const auto &x_shape_or_data =
      infer_context->GetShapeOrDataForValue(op->operand_source(0));
  std::vector<symbol::DimExpr> input_dims = x_shape_or_data.shape();

  int dim = op->attribute<pir::Int32Attribute>("dim").data();

  if (dim < 0) {
    dim += input_dims.size();
  }

  std::vector<symbol::DimExpr> output_dims(input_dims.begin(),
                                           input_dims.end());
  output_dims.erase(output_dims.begin() + dim);
  // No need to add any constraints here as we are simply removing a dimension.

  infer_context->SetShapeOrDataForValue(
      op->result(0),
      symbol::ShapeOrDataDimExprs{
          symbol::TensorShapeOrDataDimExprs(output_dims)});

  return true;
}

bool AccuracyCheckOpInferSymbolicShape(
    pir::Operation *op, pir::InferSymbolicShapeContext *infer_context) {
  // The shape of output is the same as input `values` (op->operand_source(1))
  const symbol::ShapeOrDataDimExprs &operand_shape_or_data =
      infer_context->GetShapeOrDataForValue(op->operand_source(1));
  infer_context->SetShapeOrDataForValue(op->result(0), operand_shape_or_data);
  return true;
}

bool ReduceAsOpInferSymbolicShape(
    pir::Operation *op, pir::InferSymbolicShapeContext *infer_context) {
  const symbol::ShapeOrDataDimExprs &target_shape =
      infer_context->GetShapeOrDataForValue(op->operand_source(1));
  infer_context->SetShapeOrDataForValue(op->result(0), target_shape);
  return true;
}

bool TakeAlongAxisOpInferSymbolicShape(
    pir::Operation *op, pir::InferSymbolicShapeContext *infer_context) {
  // input
  const auto &arr_shape_or_data =
      infer_context->GetShapeOrDataForValue(op->operand_source(0));
  const auto &indices_shape_or_data =
      infer_context->GetShapeOrDataForValue(op->operand_source(1));
  const auto &attributes = op->attributes();
  int axis = attributes.at("axis").dyn_cast<pir::Int32Attribute>().data();

  const std::vector<symbol::DimExpr> &arr_sym_shape =
      arr_shape_or_data.data().has_value() ? arr_shape_or_data.data().value()
                                           : arr_shape_or_data.shape();
  const std::vector<symbol::DimExpr> &indices_sym_shape =
      indices_shape_or_data.data().has_value()
          ? indices_shape_or_data.data().value()
          : indices_shape_or_data.shape();

  if (axis < 0) axis += arr_sym_shape.size();

  const auto &out_sym_shape = [&] {
    std::vector<symbol::DimExpr> out_sym_shape;
    for (int i = 0; i < axis; ++i) {
      out_sym_shape.push_back(arr_sym_shape[i]);
    }
    out_sym_shape.push_back(indices_sym_shape[axis]);
    for (size_t i = axis + 1; i < arr_sym_shape.size(); ++i) {
      out_sym_shape.push_back(arr_sym_shape[i]);
    }
    return out_sym_shape;
  }();

  symbol::ShapeOrDataDimExprs shape_data{
      symbol::TensorShapeOrDataDimExprs(out_sym_shape)};

  pir::Value res = op->result(0);
  infer_context->SetShapeOrDataForValue(res, shape_data);

  return true;
}

bool TopPSamplingOpInferSymbolicShape(
    pir::Operation *op, pir::InferSymbolicShapeContext *infer_context) {
  const auto &x_dims = [op, infer_context] {
    const auto &shape_or_data =
        infer_context->GetShapeOrDataForValue(op->operand_source(0));
    if (shape_or_data.data().has_value()) {
      return shape_or_data.data().value();
    } else {
      return shape_or_data.shape();
    }
  }();

  // all the result have the same shape
  for (uint32_t rst_idx = 0; rst_idx < op->num_results(); rst_idx++) {
    const std::vector<symbol::DimExpr> out_dims{x_dims[0], 1};
    infer_context->SetShapeOrDataForValue(
        op->result(rst_idx),
        symbol::ShapeOrDataDimExprs{
            symbol::TensorShapeOrDataDimExprs(out_dims)});
  }

  return true;
}

bool TdmChildOpInferSymbolicShape(
    pir::Operation *op, pir::InferSymbolicShapeContext *infer_context) {
  const auto &x_shape_or_data =
      infer_context->GetShapeOrDataForValue(op->operand_source(0));
  const std::vector<symbol::DimExpr> &input_dims = x_shape_or_data.shape();
  int child_nums = op->attribute<pir::Int32Attribute>("child_nums").data();

  std::vector<symbol::DimExpr> output_dims = input_dims;
  output_dims.push_back(symbol::DimExpr(child_nums));

  infer_context->SetShapeOrDataForValue(
      op->result(0),
      symbol::ShapeOrDataDimExprs{
          symbol::TensorShapeOrDataDimExprs(output_dims)});
  infer_context->SetShapeOrDataForValue(
      op->result(1),
      symbol::ShapeOrDataDimExprs{
          symbol::TensorShapeOrDataDimExprs(output_dims)});

  return true;
}

// bool UnpoolOpInferSymbolicShape(pir::Operation *op,
//                                 pir::InferSymbolicShapeContext
//                                 *infer_context) {
//   // pass
//   return true;
// }

bool YoloBoxOpInferSymbolicShape(
    pir::Operation *op, pir::InferSymbolicShapeContext *infer_context) {
  const auto &x_shape_or_data =
      infer_context->GetShapeOrDataForValue(op->operand_source(0));
  const auto anchors =
      paddle::dialect::details::GetVectorAttr<int>(op, "anchors");
  int class_num = op->attribute<pir::Int32Attribute>("class_num").data();

  const std::vector<symbol::DimExpr> &x_shape = x_shape_or_data.shape();
  int anchor_num = static_cast<int>(anchors.size() / 2);

  symbol::DimExpr box_num = symbol::DimExpr(0);
  box_num = x_shape[2] * x_shape[3] * symbol::DimExpr(anchor_num);

  std::vector<symbol::DimExpr> boxes_shape = {
      x_shape[0], box_num, symbol::DimExpr(4)};
  infer_context->SetShapeOrDataForValue(
      op->result(0),
      symbol::ShapeOrDataDimExprs{
          symbol::TensorShapeOrDataDimExprs(boxes_shape)});

  std::vector<symbol::DimExpr> scores_shape = {
      x_shape[0], box_num, symbol::DimExpr(class_num)};
  infer_context->SetShapeOrDataForValue(
      op->result(1),
      symbol::ShapeOrDataDimExprs{
          symbol::TensorShapeOrDataDimExprs(scores_shape)});

  return true;
}

}  // namespace paddle::dialect

namespace cinn::dialect {
using paddle::dialect::IscloseOpInferSymbolicShape;
}
