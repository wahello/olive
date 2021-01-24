#include "nodetreeview.h"

namespace olive {

NodeTreeView::NodeTreeView(QWidget *parent) :
  QTreeWidget(parent),
  only_show_keyframable_(false),
  show_keyframe_tracks_as_rows_(false)
{
  connect(this, &NodeTreeView::itemChanged, this, &NodeTreeView::ItemCheckStateChanged);
  connect(this, &NodeTreeView::itemSelectionChanged, this, &NodeTreeView::SelectionChanged);

  Retranslate();
}

bool NodeTreeView::IsNodeEnabled(Node *n) const
{
  return !disabled_nodes_.contains(n);
}

bool NodeTreeView::IsInputEnabled(NodeInput *i, int element, int track) const
{
  return !disabled_inputs_.contains({i, element, track});
}

void NodeTreeView::SetNodes(const QVector<Node *> &nodes)
{
  nodes_ = nodes;

  this->clear();

  foreach (Node* n, nodes_) {
    QTreeWidgetItem* node_item = new QTreeWidgetItem();
    node_item->setText(0, n->Name());
    node_item->setCheckState(0, disabled_nodes_.contains(n) ? Qt::Unchecked : Qt::Checked);
    node_item->setData(0, kItemType, kItemTypeNode);
    node_item->setData(0, kItemPointer, reinterpret_cast<quintptr>(n));

    foreach (NodeInput* input, n->inputs()) {
      if (only_show_keyframable_ && !input->IsKeyframable()) {
        continue;
      }

      int type_track_count = NodeValue::get_number_of_keyframe_tracks(input->GetDataType());
      bool type_has_multiple_tracks = (type_track_count > 1);

      QTreeWidgetItem* input_item = CreateItem(node_item, input, -1, type_has_multiple_tracks ? -1 : 0);

      if (input->IsArray()) {
        for (int i=0; i<input->ArraySize(); i++) {
          QTreeWidgetItem* element_item = CreateItem(input_item, input, i, type_has_multiple_tracks ? -1 : 0);

          if (type_has_multiple_tracks && show_keyframe_tracks_as_rows_) {
            for (int j=0; j<type_track_count; j++) {
              CreateItem(element_item, input, i, j);
            }
          }
        }
      } else if (type_has_multiple_tracks && show_keyframe_tracks_as_rows_) {
        for (int j=0; j<type_track_count; j++) {
          CreateItem(input_item, input, -1, j);
        }
      }

    }

    // Add at the end to prevent unnecessary signalling while we're setting these objects up
    if (node_item->childCount() > 0) {
      this->addTopLevelItem(node_item);
    } else {
      delete node_item;
    }
  }
}

void NodeTreeView::changeEvent(QEvent *e)
{
  QTreeWidget::changeEvent(e);

  if (e->type() == QEvent::LanguageChange) {
    Retranslate();
  }
}

void NodeTreeView::mouseDoubleClickEvent(QMouseEvent *e)
{
  QTreeWidget::mouseDoubleClickEvent(e);

  NodeInput::KeyframeTrackReference ref = GetSelectedInput();

  if (ref.input) {
    emit InputDoubleClicked(ref.input, ref.element, ref.track);
  }
}

void NodeTreeView::Retranslate()
{
  setHeaderLabel(tr("Nodes"));
}

NodeInput::KeyframeTrackReference NodeTreeView::GetSelectedInput()
{
  QList<QTreeWidgetItem*> sel = selectedItems();

  NodeInput* selected_input = nullptr;
  int selected_element = -1;
  int selected_track = -1;

  if (!sel.isEmpty()) {
    QTreeWidgetItem* item = sel.first();

    if (item->data(0, kItemType).toInt() == kItemTypeInput) {
      selected_input = reinterpret_cast<NodeInput*>(item->data(0, kItemPointer).value<quintptr>());
      selected_element = item->data(0, kItemElement).toInt();
      selected_track = item->data(0, kItemTrack).toInt();
    }
  }

  return {selected_input, selected_element, selected_track};
}

QTreeWidgetItem* NodeTreeView::CreateItem(QTreeWidgetItem *parent, NodeInput *input, int element, int track)
{
  QTreeWidgetItem* input_item = new QTreeWidgetItem(parent);

  QString item_name;
  if (track == -1 || NodeValue::get_number_of_keyframe_tracks(input->GetDataType()) == 1) {
    item_name = input->name();
  } else {
    switch (track) {
    case 0:
      item_name = tr("X");
      break;
    case 1:
      item_name = tr("Y");
      break;
    case 2:
      item_name = tr("Z");
      break;
    case 3:
      item_name = tr("W");
      break;
    default:
      item_name = QString::number(track);
    }
  }
  input_item->setText(0, item_name);

  input_item->setCheckState(0, disabled_inputs_.contains({input, element, track}) ? Qt::Unchecked : Qt::Checked);
  input_item->setData(0, kItemType, kItemTypeInput);
  input_item->setData(0, kItemPointer, reinterpret_cast<quintptr>(input));
  input_item->setData(0, kItemElement, element);
  input_item->setData(0, kItemTrack, track);

  return input_item;
}

void NodeTreeView::ItemCheckStateChanged(QTreeWidgetItem *item, int column)
{
  Q_UNUSED(column)

  switch (item->data(0, kItemType).toInt()) {
  case kItemTypeNode:
  {
    Node* n = reinterpret_cast<Node*>(item->data(0, kItemPointer).value<quintptr>());

    if (item->checkState(0) == Qt::Checked) {
      if (disabled_nodes_.contains(n)) {
        disabled_nodes_.removeOne(n);
        emit NodeEnableChanged(n, true);
      }
    } else if (!disabled_nodes_.contains(n)) {
      disabled_nodes_.append(n);
      emit NodeEnableChanged(n, false);
    }
    break;
  }
  case kItemTypeInput:
  {
    NodeInput* input = reinterpret_cast<NodeInput*>(item->data(0, kItemPointer).value<quintptr>());
    int element = item->data(0, kItemElement).toInt();
    int track = item->data(0, kItemTrack).toInt();
    NodeInput::KeyframeTrackReference i = {input, element, track};

    if (item->checkState(0) == Qt::Checked) {
      if (disabled_inputs_.contains(i)) {
        disabled_inputs_.removeOne(i);
        emit InputEnableChanged(input, element, track, true);
      }
    } else if (!disabled_inputs_.contains(i)) {
      disabled_inputs_.append(i);
      emit InputEnableChanged(input, element, track, false);
    }
    break;
  }
  }
}

void NodeTreeView::SelectionChanged()
{
  NodeInput::KeyframeTrackReference ref = GetSelectedInput();

  emit InputSelectionChanged(ref.input, ref.element, ref.track);
}

}
