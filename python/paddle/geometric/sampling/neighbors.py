#   Copyright (c) 2022 PaddlePaddle Authors. All Rights Reserved.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

from __future__ import annotations

from typing import TYPE_CHECKING, Literal, overload

from paddle import _C_ops
from paddle.base.data_feeder import check_variable_and_dtype
from paddle.base.layer_helper import LayerHelper
from paddle.framework import in_dynamic_or_pir_mode

if TYPE_CHECKING:
    from paddle import Tensor
__all__ = []


@overload
def sample_neighbors(
    row: Tensor,
    colptr: Tensor,
    input_nodes: Tensor,
    sample_size: int = ...,
    eids: Tensor | None = ...,
    return_eids: Literal[True] = ...,
    perm_buffer: Tensor | None = ...,
    name: str | None = ...,
) -> tuple[Tensor, Tensor, Tensor]: ...


@overload
def sample_neighbors(
    row: Tensor,
    colptr: Tensor,
    input_nodes: Tensor,
    sample_size: int = ...,
    eids: Tensor | None = ...,
    return_eids: Literal[False] = ...,
    perm_buffer: Tensor | None = ...,
    name: str | None = ...,
) -> tuple[Tensor, Tensor]: ...


@overload
def sample_neighbors(
    row: Tensor,
    colptr: Tensor,
    input_nodes: Tensor,
    sample_size: int = ...,
    eids: Tensor | None = ...,
    return_eids: bool = ...,
    perm_buffer: Tensor | None = ...,
    name: str | None = ...,
) -> tuple[Tensor, Tensor] | tuple[Tensor, Tensor, Tensor]: ...


def sample_neighbors(
    row,
    colptr,
    input_nodes,
    sample_size=-1,
    eids=None,
    return_eids=False,
    perm_buffer=None,
    name=None,
):
    """

    Graph Sample Neighbors API.

    This API is mainly used in Graph Learning domain, and the main purpose is to
    provide high performance of graph sampling method. For example, we get the
    CSC(Compressed Sparse Column) format of the input graph edges as `row` and
    `colptr`, so as to convert graph data into a suitable format for sampling.
    `input_nodes` means the nodes we need to sample neighbors, and `sample_sizes`
    means the number of neighbors and number of layers we want to sample.

    Besides, we support fisher-yates sampling in GPU version.

    Args:
        row (Tensor): One of the components of the CSC format of the input graph, and
                      the shape should be [num_edges, 1] or [num_edges]. The available
                      data type is int32, int64.
        colptr (Tensor): One of the components of the CSC format of the input graph,
                         and the shape should be [num_nodes + 1, 1] or [num_nodes + 1].
                         The data type should be the same with `row`.
        input_nodes (Tensor): The input nodes we need to sample neighbors for, and the
                              data type should be the same with `row`.
        sample_size (int, optional): The number of neighbors we need to sample. Default value is -1,
                           which means returning all the neighbors of the input nodes.
        eids (Tensor, optional): The eid information of the input graph. If return_eids is True,
                            then `eids` should not be None. The data type should be the
                            same with `row`. Default is None.
        return_eids (bool, optional): Whether to return eid information of sample edges. Default is False.
        perm_buffer (Tensor, optional): Permutation buffer for fisher-yates sampling. If `use_perm_buffer`
                              is True, then `perm_buffer` should not be None. The data type should
                              be the same with `row`. If not None, we will use fiser-yates sampling
                              to speed up. Only useful for gpu version. Default is None.
        name (str, optional): Name for the operation (optional, default is None).
                              For more information, please refer to :ref:`api_guide_Name`.

    Returns:
        - out_neighbors (Tensor), the sample neighbors of the input nodes.

        - out_count (Tensor), the number of sampling neighbors of each input node, and the shape
          should be the same with `input_nodes`.

        - out_eids (Tensor), if `return_eids` is True, we will return the eid information of the
          sample edges.

    Examples:
        .. code-block:: python

            >>> import paddle

            >>> # edges: (3, 0), (7, 0), (0, 1), (9, 1), (1, 2), (4, 3), (2, 4),
            >>> #        (9, 5), (3, 5), (9, 6), (1, 6), (9, 8), (7, 8)
            >>> row = [3, 7, 0, 9, 1, 4, 2, 9, 3, 9, 1, 9, 7]
            >>> colptr = [0, 2, 4, 5, 6, 7, 9, 11, 11, 13, 13]
            >>> nodes = [0, 8, 1, 2]
            >>> sample_size = 2
            >>> row = paddle.to_tensor(row, dtype="int64")
            >>> colptr = paddle.to_tensor(colptr, dtype="int64")
            >>> nodes = paddle.to_tensor(nodes, dtype="int64")
            >>> out_neighbors, out_count = paddle.geometric.sample_neighbors(row, colptr, nodes, sample_size=sample_size)

    """

    if return_eids:
        if eids is None:
            raise ValueError(
                "`eids` should not be None if `return_eids` is True."
            )

    use_perm_buffer = True if perm_buffer is not None else False

    if in_dynamic_or_pir_mode():
        (
            out_neighbors,
            out_count,
            out_eids,
        ) = _C_ops.graph_sample_neighbors(
            row,
            colptr,
            input_nodes,
            eids,
            perm_buffer,
            sample_size,
            return_eids,
            use_perm_buffer,
        )
        if return_eids:
            return out_neighbors, out_count, out_eids
        return out_neighbors, out_count

    check_variable_and_dtype(
        row, "Row", ("int32", "int64"), "graph_sample_neighbors"
    )
    check_variable_and_dtype(
        colptr, "Col_Ptr", ("int32", "int64"), "graph_sample_neighbors"
    )
    check_variable_and_dtype(
        input_nodes, "X", ("int32", "int64"), "graph_sample_neighbors"
    )
    if return_eids:
        check_variable_and_dtype(
            eids, "Eids", ("int32", "int64"), "graph_sample_neighbors"
        )
    if use_perm_buffer:
        check_variable_and_dtype(
            perm_buffer,
            "Perm_Buffer",
            ("int32", "int64"),
            "graph_sample_neighbors",
        )

    helper = LayerHelper("sample_neighbors", **locals())
    out_neighbors = helper.create_variable_for_type_inference(dtype=row.dtype)
    out_count = helper.create_variable_for_type_inference(dtype=row.dtype)
    out_eids = helper.create_variable_for_type_inference(dtype=row.dtype)
    helper.append_op(
        type="graph_sample_neighbors",
        inputs={
            "Row": row,
            "Col_Ptr": colptr,
            "X": input_nodes,
            "Eids": eids if return_eids else None,
            "Perm_Buffer": perm_buffer if use_perm_buffer else None,
        },
        outputs={
            "Out": out_neighbors,
            "Out_Count": out_count,
            "Out_Eids": out_eids,
        },
        attrs={
            "sample_size": sample_size,
            "return_eids": return_eids,
            "flag_perm_buffer": use_perm_buffer,
        },
    )
    if return_eids:
        return out_neighbors, out_count, out_eids
    return out_neighbors, out_count


@overload
def weighted_sample_neighbors(
    row: Tensor,
    colptr: Tensor,
    edge_weight: Tensor,
    input_nodes: Tensor,
    sample_size: int = ...,
    eids: Tensor | None = ...,
    return_eids: Literal[True] = ...,
    name: str | None = ...,
) -> tuple[Tensor, Tensor, Tensor]: ...


@overload
def weighted_sample_neighbors(
    row: Tensor,
    colptr: Tensor,
    edge_weight: Tensor,
    input_nodes: Tensor,
    sample_size: int = ...,
    eids: Tensor | None = ...,
    return_eids: Literal[False] = ...,
    name: str | None = ...,
) -> tuple[Tensor, Tensor]: ...


@overload
def weighted_sample_neighbors(
    row: Tensor,
    colptr: Tensor,
    edge_weight: Tensor,
    input_nodes: Tensor,
    sample_size: int = ...,
    eids: Tensor | None = ...,
    return_eids: bool = ...,
    name: str | None = ...,
) -> tuple[Tensor, Tensor] | tuple[Tensor, Tensor, Tensor]: ...


def weighted_sample_neighbors(
    row,
    colptr,
    edge_weight,
    input_nodes,
    sample_size=-1,
    eids=None,
    return_eids=False,
    name=None,
):
    """
    Graph Weighted Sample Neighbors API.

    This API is mainly used in Graph Learning domain, and the main purpose is to
    provide high performance of graph weighted-sampling method. For example, we get the
    CSC(Compressed Sparse Column) format of the input graph edges as `row` and
    `colptr`, so as to convert graph data into a suitable format for sampling, and the
    input `edge_weight` should also match the CSC format. Besides, `input_nodes` means
    the nodes we need to sample neighbors, and `sample_sizes` means the number of neighbors
    and number of layers we want to sample. This API will finally return the weighted sampled
    neighbors, and the probability of being selected as a neighbor is related to its weight,
    with higher weight and higher probability.

    Args:
        row (Tensor): One of the components of the CSC format of the input graph, and
                      the shape should be [num_edges, 1] or [num_edges]. The available
                      data type is int32, int64.
        colptr (Tensor): One of the components of the CSC format of the input graph,
                         and the shape should be [num_nodes + 1, 1] or [num_nodes + 1].
                         The data type should be the same with `row`.
        edge_weight (Tensor): The edge weight of the CSC format graph edges. And the shape
                              should be [num_edges, 1] or [num_edges]. The available data
                              type is float32.
        input_nodes (Tensor): The input nodes we need to sample neighbors for, and the
                              data type should be the same with `row`.
        sample_size (int, optional): The number of neighbors we need to sample. Default value is -1,
                           which means returning all the neighbors of the input nodes.
        eids (Tensor, optional): The eid information of the input graph. If return_eids is True,
                            then `eids` should not be None. The data type should be the
                            same with `row`. Default is None.
        return_eids (bool, optional): Whether to return eid information of sample edges. Default is False.
        name (str, optional): Name for the operation (optional, default is None).
                              For more information, please refer to :ref:`api_guide_Name`.

    Returns:
        - out_neighbors (Tensor), the sample neighbors of the input nodes.

        - out_count (Tensor), the number of sampling neighbors of each input node, and the shape
          should be the same with `input_nodes`.

        - out_eids (Tensor), if `return_eids` is True, we will return the eid information of the
          sample edges.

    Examples:
        .. code-block:: python

            >>> import paddle

            >>> # edges: (3, 0), (7, 0), (0, 1), (9, 1), (1, 2), (4, 3), (2, 4),
            >>> #        (9, 5), (3, 5), (9, 6), (1, 6), (9, 8), (7, 8)
            >>> row = [3, 7, 0, 9, 1, 4, 2, 9, 3, 9, 1, 9, 7]
            >>> colptr = [0, 2, 4, 5, 6, 7, 9, 11, 11, 13, 13]
            >>> weight = [0.1, 0.5, 0.2, 0.5, 0.9, 1.9, 2.0, 2.1, 0.01, 0.9, 0,12, 0.59, 0.67]
            >>> nodes = [0, 8, 1, 2]
            >>> sample_size = 2
            >>> row = paddle.to_tensor(row, dtype="int64")
            >>> colptr = paddle.to_tensor(colptr, dtype="int64")
            >>> weight = paddle.to_tensor(weight, dtype="float32")
            >>> nodes = paddle.to_tensor(nodes, dtype="int64")
            >>> out_neighbors, out_count = paddle.geometric.weighted_sample_neighbors(
            ...     row, colptr, weight, nodes, sample_size=sample_size, return_eids=False
            ... )

    """

    if return_eids:
        if eids is None:
            raise ValueError(
                "`eids` should not be None if `return_eids` is True."
            )

    if in_dynamic_or_pir_mode():
        (
            out_neighbors,
            out_count,
            out_eids,
        ) = _C_ops.weighted_sample_neighbors(
            row,
            colptr,
            edge_weight,
            input_nodes,
            eids,
            sample_size,
            return_eids,
        )
        if return_eids:
            return out_neighbors, out_count, out_eids
        return out_neighbors, out_count

    check_variable_and_dtype(
        row, "row", ("int32", "int64"), "weighted_sample_neighbors"
    )
    check_variable_and_dtype(
        colptr, "colptr", ("int32", "int64"), "weighted_sample_neighbors"
    )
    check_variable_and_dtype(
        edge_weight,
        "edge_weight",
        ("float32"),
        "weighted_sample_neighbors",
    )
    check_variable_and_dtype(
        input_nodes,
        "input_nodes",
        ("int32", "int64"),
        "weighted_sample_neighbors",
    )
    if return_eids:
        check_variable_and_dtype(
            eids, "eids", ("int32", "int64"), "weighted_sample_neighbors"
        )

    helper = LayerHelper("weighted_sample_neighbors", **locals())
    out_neighbors = helper.create_variable_for_type_inference(dtype=row.dtype)
    out_count = helper.create_variable_for_type_inference(dtype=row.dtype)
    out_eids = helper.create_variable_for_type_inference(dtype=row.dtype)
    helper.append_op(
        type="weighted_sample_neighbors",
        inputs={
            "row": row,
            "colptr": colptr,
            "edge_weight": edge_weight,
            "input_nodes": input_nodes,
            "eids": eids if return_eids else None,
        },
        outputs={
            "out_neighbors": out_neighbors,
            "out_count": out_count,
            "out_eids": out_eids,
        },
        attrs={
            "sample_size": sample_size,
            "return_eids": return_eids,
        },
    )
    if return_eids:
        return out_neighbors, out_count, out_eids
    return out_neighbors, out_count
