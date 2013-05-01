/*
 * Dillo Widget
 *
 * Copyright 2013 Sebastian Geerken <sgeerken@dillo.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */


#include "outofflowmgr.hh"
#include "textblock.hh"

#include <limits.h>

using namespace lout::object;
using namespace lout::container::typed;
using namespace lout::misc;
using namespace dw::core;
using namespace dw::core::style;

namespace dw {

OutOfFlowMgr::Float::Float (OutOfFlowMgr *oofm, Widget *widget,
                            Textblock *generatingBlock, int externalIndex)
{
   this->oofm = oofm;
   this->widget = widget;
   this->generatingBlock = generatingBlock;
   this->externalIndex = externalIndex;

   yReq = yReal = size.width = size.ascent = size.descent = 0;
   dirty = true;
   inCBList = false;
}

void OutOfFlowMgr::Float::intoStringBuffer(StringBuffer *sb)
{
   sb->append ("{ widget = ");
   sb->appendPointer (widget);
   
   if (widget) {
      sb->append (" (");
      sb->append (widget->getClassName ());
      sb->append (")");
   }

   sb->append (", index = ");
   sb->appendInt (index);
   sb->append (", sideSpanningIndex = ");
   sb->appendInt (sideSpanningIndex);
   sb->append (", generatingBlock = ");
   sb->appendPointer (generatingBlock);
   sb->append (", yReq = ");
   sb->appendInt (yReq);
   sb->append (", yReal = ");
   sb->appendInt (yReal);
   sb->append (", size = { ");
   sb->appendInt (size.width);
   sb->append (", ");
   sb->appendInt (size.ascent);
   sb->append (" + ");
   sb->appendInt (size.descent);
   sb->append (" }, dirty = ");
   sb->appendBool (dirty);
   sb->append (" }, inCBList = ");
   sb->appendBool (inCBList);
   sb->append (" }");
}

int OutOfFlowMgr::Float::compareTo(Comparable *other)
{
   Float *otherFloat = (Float*)other;

   if (oofm->wasAllocated (generatingBlock)) {
      assert (oofm->wasAllocated (otherFloat->generatingBlock));
      return yForContainer() - otherFloat->yForContainer();
   } else {
      assert (generatingBlock == otherFloat->generatingBlock);
      return yReal - otherFloat->yReal;
   }
}

int OutOfFlowMgr::Float::yForTextblock (Textblock *textblock, int y)
{
   if (oofm->wasAllocated (generatingBlock)) {
      assert (oofm->wasAllocated (textblock));
      return oofm->getAllocation(generatingBlock)->y + y
         - oofm->getAllocation(textblock)->y;
   } else {
      assert (textblock == generatingBlock);
      return y;
   }
}

int OutOfFlowMgr::Float::yForContainer (int y)
{
   assert (oofm->wasAllocated (generatingBlock));
   return y + oofm->getAllocation(generatingBlock)->y -
      oofm->getAllocation(oofm->containingBlock)->y;
}

bool OutOfFlowMgr::Float::covers (Textblock *textblock, int y, int h)
{
   int reqy, fly; // either widget or canvas coordinates
   if (oofm->wasAllocated (generatingBlock)) {
      assert (oofm->wasAllocated (textblock));
      reqy = oofm->getAllocation(textblock)->y + y;
      fly = oofm->getAllocation(generatingBlock)->y + yReal;
   } else {
      assert (textblock == generatingBlock);
      reqy = y;
      fly = yReal;
   }

   oofm->ensureFloatSize (this);

   //printf ("[%p] COVERS (%p, %d, %d) => %d + %d + %d > %d && %d < %d + %d? "
   //        "%s.\n", oofm->containingBlock, textblock, y, h, fly, size.ascent,
   //        size.descent, reqy, fly, reqy, h,
   //        (fly + size.ascent + size.descent > reqy && fly < reqy + h) ?
   //        "yes" : "no");
   
   return fly + size.ascent + size.descent > reqy && fly < reqy + h;
}

int OutOfFlowMgr::Float::CompareSideSpanningIndex::compare(Object *o1,
                                                           Object *o2)
{
   return ((Float*)o1)->sideSpanningIndex - ((Float*)o2)->sideSpanningIndex;
}

int OutOfFlowMgr::SortedFloatsVector::findFloatIndex (Textblock *lastGB,
                                                      int lastExtIndex)
{
   // TODO Is the case "lastGB == NULL" (search until the end) needed?
   assert (lastGB);

   //printf ("[%p] FIND_FLOAT_INDEX (%p, %d) ...\n",
   //        oofm->containingBlock, lastGB, lastExtIndex);
           
   if (lastGB) {
      TypedPointer<Textblock> key (lastGB);
      TBInfo *tbInfo = oofm->tbInfosByTextblock->get (&key);
      if (tbInfo) {
         //printf ("      generator %p, index = %d\n",
         //        tbInfo->textblock, tbInfo->index);

         SortedFloatsVector *gbList =
            side == LEFT ? tbInfo->leftFloatsGB : tbInfo->rightFloatsGB;
         // Could be faster with binary search, but the GB (not CB!) lists
         // should be rather small.
         Float *lastFloat = NULL;
         for (int i = 0; i < gbList->size(); i++) {
            Float *vloat = gbList->get(i);
            if (vloat->externalIndex <= lastExtIndex)
               lastFloat = vloat;
         }
         
         if (lastFloat) {
            // Float found with the same generator.
            //printf ("   => %d (same generator)\n", lastFloat->index);
            return lastFloat->index;
         } else
            // Search backwards in other textblocks (but only when
            // allocated, so that CB lists make sense).
            return oofm->wasAllocated (tbInfo->textblock) ?
               findFloatIndexBackwards (tbInfo->index, lastGB,
                                        lastExtIndex) :
               -1;
      } else {
         // "lastGB" not yet registered. TODO Correct?
         //printf ("   => %d (last GB not registered)\n", size () - 1);
         return size () - 1;
      }
   } else {
      //printf ("   => %d (last GB not defined)\n", size () - 1);
      return size() - 1;
   }
}

int OutOfFlowMgr::SortedFloatsVector::findFloatIndexBackwards(int tbInfoIndex,
                                                              Textblock *lastGB,
                                                              int lastExtIndex)
{
   // No float until "lastExtIndex"; search backwards in the
   // list of text blocks.
   int last = -1; // If nothing is found.
   
   // If not allocated, the only list to search is the GB
   // list, which has been searched already.
   if (oofm->wasAllocated (lastGB)) {
      for (int index = tbInfoIndex - 1; last == -1 && index >= 0; index--) {
         TBInfo *prev = oofm->tbInfos->get (index);
         assert (index == prev->index);
         SortedFloatsVector *prevList =
            side == LEFT ? prev->leftFloatsGB : prev->rightFloatsGB;
         // Even if each GB list contains at least one elemenent
         // (otherwise it would not have been created), this one
         // element may be in the wrong (i. e. opposite) list. So,
         // this list may be empty. Also, ignore floats which are not
         // yet in the CB list. (Latter may be more efficient.)
         for (int j = prevList->size() - 1; last == -1 && j >= 0; j--) {
            Float *lastFloat = prevList->get (j);
            if (lastFloat->inCBList) {
               //printf ("      previous generator %p, index = %d; %s "
               //        "list has %d elements\n",
               //        prev->textblock, prev->index,
               //        side == LEFT ? "left" : "right",
               //        prevList->size());
               //printf ("      lastFloat: %s\n",
               //        lastFloat->toString ());
               last = lastFloat->index;
            }
         }
         // If no appropriate float found, continue.
      }
   }
   
   //printf ("   => %d (other generator)\n", last);
   return last;
}

int OutOfFlowMgr::SortedFloatsVector::find (Textblock *textblock, int y,
                                            int start, int end)
{
   Float key (oofm, NULL, NULL, 0);
   key.generatingBlock = textblock;
   key.yReal = y;
   return bsearch (&key, false, start, end);
}

int OutOfFlowMgr::SortedFloatsVector::findFirst (Textblock *textblock,
                                                 int y, int h,
                                                 Textblock *lastGB,
                                                 int lastExtIndex)
{
   int last = findFloatIndex (lastGB, lastExtIndex);
   assert (last < size());
   int i = find (textblock, y, 0, last);

   //printf ("[%p] FIND (%s, %p, allocated: %s, %d, %p, %d) => last = %d, "
   //        "result = %d (of %d)\n", oofm->containingBlock,
   //        type == GB ? "GB" : "CB", textblock,
   //        oofm->wasAllocated (textblock) ? "true" : "false", y, lastGB,
   //        lastExtIndex, last, i, size());

   // Note: The smallest value of "i" is 0, which means that "y" is before or
   // equal to the first float. The largest value is "last + 1", which means
   // that "y" is after the last float. In both cases, the first or last,
   // respectively, float is a candidate. Generally, both floats, before and
   // at the search position, are candidates.

   if (i > 0 && get(i - 1)->covers (textblock, y, h))
      return i - 1;
   else if (i <= last && get(i)->covers (textblock, y, h))
      return i;
   else
      return -1;
}

int OutOfFlowMgr::SortedFloatsVector::findLastBeforeSideSpanningIndex
   (int sideSpanningIndex)
{
   OutOfFlowMgr::Float::CompareSideSpanningIndex comparator;
   Float key (NULL, NULL, NULL, 0);
   key.sideSpanningIndex = sideSpanningIndex;
   return bsearch (&key, false, &comparator) - 1;
}

void OutOfFlowMgr::SortedFloatsVector::put (Float *vloat)
{
   lout::container::typed::Vector<Float>::put (vloat);
   vloat->index = size() - 1;
   vloat->inCBList = type == CB;
}

OutOfFlowMgr::TBInfo::TBInfo (OutOfFlowMgr *oofm, Textblock *textblock)
{
   this->textblock = textblock;
   leftFloatsGB = new SortedFloatsVector (oofm, LEFT, SortedFloatsVector::GB);
   rightFloatsGB = new SortedFloatsVector (oofm, RIGHT, SortedFloatsVector::GB);
}

OutOfFlowMgr::TBInfo::~TBInfo ()
{
   delete leftFloatsGB;
   delete rightFloatsGB;
}

OutOfFlowMgr::OutOfFlowMgr (Textblock *containingBlock)
{
   //printf ("OutOfFlowMgr::OutOfFlowMgr\n");

   this->containingBlock = containingBlock;

   leftFloatsCB = new SortedFloatsVector (this, LEFT, SortedFloatsVector::CB);
   rightFloatsCB = new SortedFloatsVector (this, RIGHT, SortedFloatsVector::CB);

   leftFloatsAll = new Vector<Float> (1, true);
   rightFloatsAll = new Vector<Float> (1, true);

   floatsByWidget = new HashTable <TypedPointer <Widget>, Float> (true, false);

   tbInfos = new Vector<TBInfo> (1, false);
   tbInfosByTextblock =
      new HashTable <TypedPointer <Textblock>, TBInfo> (true, true);

   leftFloatsMark = rightFloatsMark = 0;
   lastLeftTBIndex = lastRightTBIndex = 0;

   containingBlockWasAllocated = containingBlock->wasAllocated ();
   if (containingBlockWasAllocated)
      containingBlockAllocation = *(containingBlock->getAllocation());
}

OutOfFlowMgr::~OutOfFlowMgr ()
{
   //printf ("OutOfFlowMgr::~OutOfFlowMgr\n");

   delete leftFloatsCB;
   delete rightFloatsCB;

   // Order is important: tbInfosByTextblock is owner of the instances
   // of TBInfo.tbInfosByTextblock
   delete tbInfos;
   delete tbInfosByTextblock;

   delete floatsByWidget;

   // Order is important, since the instances of Float are owned by
   // leftFloatsAll and rightFloatsAll, so these should be deleted
   // last.
   delete leftFloatsAll;
   delete rightFloatsAll;
}

void OutOfFlowMgr::sizeAllocateStart (Allocation *containingBlockAllocation)
{
   this->containingBlockAllocation = *containingBlockAllocation;
   containingBlockWasAllocated = true;
}

void OutOfFlowMgr::sizeAllocateEnd ()
{
   //printf ("[%p] SIZE_ALLOCATE_END: leftFloatsMark = %d, "
   //        "rightFloatsMark = %d\n",
   //        containingBlock, leftFloatsMark, rightFloatsMark);

   // 1. Move floats from GB lists to the one CB list.
   moveFromGBToCB (LEFT);
   moveFromGBToCB (RIGHT);
      
   // 2. Floats have to be allocated
   sizeAllocateFloats (LEFT);
   sizeAllocateFloats (RIGHT);

   // 3. Textblocks have already been allocated, but we store some
   // information for later use. TODO: Update this comment!
   for (lout::container::typed::Iterator<TypedPointer <Textblock> > it =
           tbInfosByTextblock->iterator ();
        it.hasNext (); ) {
      TypedPointer <Textblock> *key = it.getNext ();
      TBInfo *tbInfo = tbInfosByTextblock->get (key);
      Textblock *tb = key->getTypedValue();

      Allocation *tbAllocation = getAllocation (tb);
      int xCB = tbAllocation->x - containingBlockAllocation.x;
      int yCB = tbAllocation->y - containingBlockAllocation.y;
      int width = tbAllocation->width;
      int height = tbAllocation->ascent + tbAllocation->descent;

      if ((!tbInfo->wasAllocated || tbInfo->xCB != xCB || tbInfo->yCB != yCB ||
           tbInfo->width != width || tbInfo->height != height)) {
         int oldPos, newPos;
         Widget *oldFloat, *newFloat;
         // To calculate the minimum, both allocations, old and new,
         // have to be tested.
      
         // Old allocation:
         bool c1 = isTextblockCoveredByFloats (tb,
                                               tbInfo->xCB
                                               + containingBlockAllocation.x,
                                               tbInfo->yCB
                                               + containingBlockAllocation.y,
                                               tbInfo->width, tbInfo->height,
                                               &oldPos, &oldFloat);
         // new allocation:
         int c2 = isTextblockCoveredByFloats (tb, tbAllocation->x,
                                              tbAllocation->y, width,
                                              height, &newPos, &newFloat);
         if (c1 || c2) {
            if (!c1)
               tb->borderChanged (newPos, newFloat);
            else if (!c2)
               tb->borderChanged (oldPos, oldFloat);
            else {
               if (oldPos < newPos)
                  tb->borderChanged (oldPos, oldFloat);
               else
                  tb->borderChanged (newPos, newFloat);
            }
         }
      }
      
      tbInfo->wasAllocated = true;
      tbInfo->xCB = xCB;
      tbInfo->yCB = yCB;
      tbInfo->width = width;
      tbInfo->height = height;
   }
}

bool OutOfFlowMgr::isTextblockCoveredByFloats (Textblock *tb, int tbx, int tby,
                                               int tbWidth, int tbHeight,
                                               int *floatPos, Widget **vloat)
{
   int leftPos, rightPos;
   Widget *leftFloat, *rightFloat;
   bool c1 = isTextblockCoveredByFloats (leftFloatsCB, tb, tbx, tby,
                                         tbWidth, tbHeight, &leftPos,
                                         &leftFloat);
   bool c2 = isTextblockCoveredByFloats (rightFloatsCB, tb, tbx, tby,
                                         tbWidth, tbHeight, &rightPos,
                                         &rightFloat);
   if (c1 || c2) {
      if (!c1) {
         *floatPos = rightPos;
         *vloat = rightFloat;
      } else if (!c2) {
         *floatPos = leftPos;
         *vloat = leftFloat;
      } else {
         if (leftPos < rightPos) {
            *floatPos = leftPos;
            *vloat = leftFloat;
         } else{ 
            *floatPos = rightPos;
            *vloat = rightFloat;
         }
      }
   }

   return c1 || c2;
}

bool OutOfFlowMgr::isTextblockCoveredByFloats (SortedFloatsVector *list,
                                               Textblock *tb, int tbx, int tby,
                                               int tbWidth, int tbHeight,
                                               int *floatPos, Widget **vloat)
{
   *floatPos = INT_MAX;
   bool covered = false;

   for (int i = 0; i < list->size(); i++) {
      // TODO binary search
      Float *v = list->get(i);

      assert (wasAllocated (v->generatingBlock));

      if (tb != v->generatingBlock) {
         int flh = v->dirty ? 0 : v->size.ascent + v->size.descent;
         int y1 = getAllocation(v->generatingBlock)->y + v->yReal;
         int y2 = y1 + flh;
         
         // TODO: Also regard horizontal dimension (same for tellPositionOrNot).
         if (y2 > tby && y1 < tby + tbHeight) {
            covered = true;
            if (y1 - tby < *floatPos) {
               *floatPos = y1 - tby;
               *vloat = v->widget;
            }
         }
      }

      // All floarts are searched, to find the minimum. TODO: Are
      // floats sorted, so this can be shortene? (The first is the
      // minimum?)
   }

   return covered;
}

void OutOfFlowMgr::moveFromGBToCB (Side side)
{
   SortedFloatsVector *dest = side == LEFT ? leftFloatsCB : rightFloatsCB;
   int *floatsMark = side == LEFT ? &leftFloatsMark : &rightFloatsMark;

   for (int mark = 0; mark <= *floatsMark; mark++)
      for (lout::container::typed::Iterator<TBInfo> it = tbInfos->iterator ();
           it.hasNext (); ) {
         TBInfo *tbInfo = it.getNext ();
         SortedFloatsVector *src =
            side == LEFT ? tbInfo->leftFloatsGB : tbInfo->rightFloatsGB;
         for (int i = 0; i < src->size (); i++) {
            Float *vloat = src->get(i);
            if (!vloat->inCBList && vloat->mark == mark) {
               dest->put (vloat);
               //printf("[%p] moving %s float %p (%s %p, mark %d) to CB list\n",
               //       containingBlock, side == LEFT ? "left" : "right",
               //       vloat, vloat->widget->getClassName(), vloat->widget,
               //       vloat->mark);
            }
         }
      }

   *floatsMark = 0;

   /* Old code: GB lists do not have to be cleared, but their contents
      are still useful after allocation. Soon to be deleted, not only
      uncommented.
      
   for (lout::container::typed::Iterator<TBInfo> it = tbInfos->iterator ();
        it.hasNext (); ) {
      TBInfo *tbInfo = it.getNext ();
      SortedFloatsVector *src =
         side == LEFT ? tbInfo->leftFloatsGB : tbInfo->rightFloatsGB;
      src->clear ();
   }
   */

   //printf ("[%p] new %s list:\n",
   //        containingBlock, side == LEFT ? "left" : "right");
   //for (int i = 0; i < dest->size(); i++)
   //   printf ("   %d: %s\n", i, dest->get(i)->toString());
}

void OutOfFlowMgr::sizeAllocateFloats (Side side)
{
   SortedFloatsVector *list = side == LEFT ? leftFloatsCB : rightFloatsCB;

   for (int i = 0; i < list->size(); i++) {
      // TODO Missing: check newly calculated positions, collisions,
      // and queue resize, when neccessary. TODO: See step 2?

      Float *vloat = list->get(i);
      ensureFloatSize (vloat);

      Allocation *gbAllocation = getAllocation(vloat->generatingBlock);
      Allocation childAllocation;
      switch (side) {
      case LEFT:
         childAllocation.x = gbAllocation->x
            + vloat->generatingBlock->getStyle()->boxOffsetX();
         break;

      case RIGHT:
         childAllocation.x =
            gbAllocation->x
            + min (gbAllocation->width, vloat->generatingBlock->getAvailWidth())
            - vloat->size.width
            - vloat->generatingBlock->getStyle()->boxRestWidth();
         break;
      }

      childAllocation.y = gbAllocation->y + vloat->yReal;
      childAllocation.width = vloat->size.width;
      childAllocation.ascent = vloat->size.ascent;
      childAllocation.descent = vloat->size.descent;
      
      vloat->widget->sizeAllocate (&childAllocation);

      //printf ("allocate %s #%d -> (%d, %d), %d x (%d + %d)\n",
      //        right ? "right" : "left", i, childAllocation.x,
      //        childAllocation.y, childAllocation.width,
      //        childAllocation.ascent, childAllocation.descent);
   }
}



void OutOfFlowMgr::draw (View *view, Rectangle *area)
{
   draw (leftFloatsCB, view, area);
   draw (rightFloatsCB, view, area);
}

void OutOfFlowMgr::draw (SortedFloatsVector *list, View *view, Rectangle *area)
{
   // This could be improved, since the list is sorted: search the
   // first float fitting into the area, and iterate until one is
   // found below the area.
   for (int i = 0; i < list->size(); i++) {
      Float *vloat = list->get(i);
      core::Rectangle childArea;
      if (vloat->widget->intersects (area, &childArea))
         vloat->widget->draw (view, &childArea);
   }
}

bool OutOfFlowMgr::isWidgetOutOfFlow (core::Widget *widget)
{
   // Will be extended for absolute positions.
   return widget->getStyle()->vloat != FLOAT_NONE;
}

void OutOfFlowMgr::addWidget (Widget *widget, Textblock *generatingBlock,
                              int externalIndex)
{
   if (widget->getStyle()->vloat != FLOAT_NONE) {
      TBInfo *tbInfo = registerCaller (generatingBlock);

      Float *vloat = new Float (this, widget, generatingBlock, externalIndex);

      switch (widget->getStyle()->vloat) {
      case FLOAT_LEFT:
         leftFloatsAll->put (vloat);
         widget->parentRef = createRefLeftFloat (leftFloatsAll->size() - 1);

         if (wasAllocated (generatingBlock)) {
            leftFloatsCB->put (vloat);
            //printf ("[%p] adding left float %p (%s %p) to CB list\n",
            //        containingBlock, vloat, widget->getClassName(), widget);
         } else {
            if (tbInfo->index < lastLeftTBIndex)
               leftFloatsMark++;

            tbInfo->leftFloatsGB->put (vloat);
            vloat->mark = leftFloatsMark;
            //printf ("[%p] adding left float %p (%s %p, mark %d) to GB list "
            //        "(index %d, last = %d)\n",
            //        containingBlock, vloat, widget->getClassName(), widget,
            //        vloat->mark, tbInfo->index, lastLeftTBIndex);

            lastLeftTBIndex = tbInfo->index;
         }
         break;

      case FLOAT_RIGHT:
         rightFloatsAll->put (vloat);
         widget->parentRef = createRefRightFloat (rightFloatsAll->size() - 1);

         if (wasAllocated (generatingBlock)) {
            rightFloatsCB->put (vloat);
            //printf ("[%p] adding right float %p (%s %p) to CB list\n",
            //        containingBlock, vloat, widget->getClassName(), widget);
         } else {
            if (tbInfo->index < lastRightTBIndex)
               rightFloatsMark++;

            tbInfo->rightFloatsGB->put (vloat);
            vloat->mark = rightFloatsMark;
            //printf ("[%p] adding right float %p (%s %p, mark %d) to GB list "
            //        "(index %d, last = %d)\n",
            //        containingBlock, vloat, widget->getClassName(), widget,
            //        vloat->mark, tbInfo->index, lastRightTBIndex);

            lastRightTBIndex = tbInfo->index;
         }

         break;

      default:
         assertNotReached();
      }

      // "sideSpanningIndex" is only compared, so this simple
      // assignment is sufficient; differenciation between GB and CB
      // lists is not neccessary. TODO: Can this also be applied to
      // "index", to simplify the current code? Check: where is
      // "index" used.
      vloat->sideSpanningIndex =
         leftFloatsAll->size() + rightFloatsAll->size() - 1;

      floatsByWidget->put (new TypedPointer<Widget> (widget), vloat);
   } else
      // Will continue here for absolute positions.
      assertNotReached();
}

void OutOfFlowMgr::moveExternalIndices (Textblock *generatingBlock,
                                        int oldStartIndex, int diff)
{
   TypedPointer<Textblock> key (generatingBlock);
   TBInfo *tbInfo = tbInfosByTextblock->get (&key);
   if (tbInfo) {
      moveExternalIndices (tbInfo->leftFloatsGB, oldStartIndex, diff);
      moveExternalIndices (tbInfo->rightFloatsGB, oldStartIndex, diff);
   }
   // Not neccessary registered, i. e. no new TBInfo is created.
}

void OutOfFlowMgr::moveExternalIndices (SortedFloatsVector *list,
                                        int oldStartIndex, int diff)
{
   // Could be faster with binary search, but the GB (not CB!) lists
   // should be rather small.
   for (int i = 0; i < list->size(); i++) {
      Float *vloat = list->get(i);
      if (vloat->externalIndex >= oldStartIndex)
         vloat->externalIndex += diff;
   }
}

OutOfFlowMgr::Float *OutOfFlowMgr::findFloatByWidget (Widget *widget)
{
   TypedPointer <Widget> key (widget);
   Float *vloat = floatsByWidget->get (&key);
   assert (vloat != NULL);
   return vloat;
}

void OutOfFlowMgr::markSizeChange (int ref)
{
   //printf ("[%p] MARK_SIZE_CHANGE (%d)\n", containingBlock, ref);

   if (isRefFloat (ref)) {
      Float *vloat;
      
      if (isRefLeftFloat (ref)) {
         int i = getFloatIndexFromRef (ref);
         vloat = leftFloatsAll->get (i);
         //printf ("   => left float %d\n", i);
      } else if (isRefRightFloat (ref)) {
         int i = getFloatIndexFromRef (ref);
         vloat = rightFloatsAll->get (i);
         //printf ("   => right float %d\n", i);
      } else {
         assertNotReached();
         vloat = NULL; // compiler happiness
      }
      
      vloat->dirty = true;
      // TODO May cause problems (endless resizing?) when float has no
      // defined position.
      vloat->generatingBlock->borderChanged (vloat->yReal, vloat->widget);
   } else
      // later: absolute positions
      assertNotReached();
}


void OutOfFlowMgr::markExtremesChange (int ref)
{
   // Nothing to do here.
}

Widget *OutOfFlowMgr::getWidgetAtPoint (int x, int y, int level)
{
   Widget *childAtPoint = getWidgetAtPoint (leftFloatsCB, x, y, level);
   if (childAtPoint == NULL)
      childAtPoint = getWidgetAtPoint (rightFloatsCB, x, y, level);
   return childAtPoint;
}

Widget *OutOfFlowMgr::getWidgetAtPoint (SortedFloatsVector *list,
                                        int x, int y, int level)
{
   for (int i = 0; i < list->size(); i++) {
      // Could use binary search to be faster.
      Float *vloat = list->get(i);
      Widget *childAtPoint = vloat->widget->getWidgetAtPoint (x, y, level + 1);
      if (childAtPoint)
         return childAtPoint;
   }

   return NULL;
}


void OutOfFlowMgr::tellPosition (Widget *widget, int yReq)
{
   assert (yReq >= 0);

   Float *vloat = findFloatByWidget(widget);

   //printf ("[%p] TELL_POSITION_OR_NOT (%p (%s), %d)\n",
   //        containingBlock, widget, widget->getClassName (), yReq);
   //printf ("   this float: %s\n", vloat->toString());

   SortedFloatsVector *listSame, *listOpp;
   getFloatsLists (vloat, &listSame, &listOpp);
   ensureFloatSize (vloat);
   //printf ("   ensured size: %s\n", vloat->toString());

   //printf ("   all floats on same side (%s):\n",
   //        listSame->type == SortedFloatsVector::GB ? "GB" : "CB");
   //for (int i = 0; i < listSame->size(); i++)
   //   printf ("      %d: %p, %s\n", i, listSame->get(i),
   //           listSame->get(i)->toString());

   int oldY = vloat->yReal;

   // "yReal" may change due to collisions (see below).
   vloat->yReq = vloat->yReal = yReq;

   // Test collisions (on this side). Only previous float is relevant.
   int yRealNew;
   if (vloat->index >= 1 &&
       collides (vloat, listSame->get (vloat->index - 1), &yRealNew)) {
      vloat->yReal = yRealNew;
      //printf ("   collides; yReal = %d\n", vloat->yReal);
   }

   // Test collisions (on the opposite side). Search the last float on
   // the other size before this float; only this is relevant.
   int lastOppFloat =
      listOpp->findLastBeforeSideSpanningIndex (vloat->sideSpanningIndex);
   if (lastOppFloat >= 0) {
      Float *last = listOpp->get (lastOppFloat);
      if (collides (vloat, last, &yRealNew)) {
         // Here, test also horizontal values.
         bool collidesH;
         if (vloat->generatingBlock == last->generatingBlock)
            collidesH = vloat->size.width + last->size.width +
               vloat->generatingBlock->getStyle()->boxDiffWidth()
               > vloat->generatingBlock->getAvailWidth();
         else {
            // Here (different generating blocks) it can be assumed
            // that the allocations are defined, otherwise, the float
            // "last" would not be found in "listOpp".
            assert (wasAllocated (vloat->generatingBlock));
            assert (wasAllocated (last->generatingBlock));
            Float *left, *right;
            if (widget->getStyle()->vloat == FLOAT_LEFT) {
               left = vloat;
               right = last;
            } else {
               left = last;
               right = vloat;
            }
            
            // right border of the left float (canvas coordinates)
            int rightOfLeft =
               left->generatingBlock->getAllocation()->x
               + left->generatingBlock->getStyle()->boxOffsetX()
               + left->size.width;
            // left border of the right float (canvas coordinates)
            int leftOfRight =
               right->generatingBlock->getAllocation()->x
               + min (right->generatingBlock->getAllocation()->width,
                      right->generatingBlock->getAvailWidth())
               - right->generatingBlock->getStyle()->boxRestWidth()
               - right->size.width;
            
            collidesH = rightOfLeft > leftOfRight;
         }
         
         if (collidesH)
            vloat->yReal = yRealNew;
      }
   }

   // No call neccessary when yReal has not changed. (Notice that
   // checking for yReq is wrong: yReq may remain the same, when yReal
   // changes, e. g. when previous float has changes its size.
   if (vloat->yReal != oldY)
      checkCoverage (vloat, oldY);
}

bool OutOfFlowMgr::collides (Float *vloat, Float *other, int *yReal)
{
   ensureFloatSize (other);
   int otherH = other->size.ascent + other->size.descent;

   if (wasAllocated (other->generatingBlock)) {
      assert (wasAllocated (vloat->generatingBlock));
      
      if (getAllocation(vloat->generatingBlock)->y + vloat->yReal <
          getAllocation(other->generatingBlock)->y + other->yReal + otherH) {
         *yReal =
            getAllocation(other->generatingBlock)->y + other->yReal
            + otherH - getAllocation(vloat->generatingBlock)->y;
         return true;
      }
   } else {
      assert (vloat->generatingBlock == other->generatingBlock);
      
      if (vloat->yReal < other->yReal + otherH) {
         *yReal = other->yReal + otherH;
         return true;
      }
   }

   return false;
}


void OutOfFlowMgr::checkCoverage (Float *vloat, int oldY)
{
   // Only this float has been changed (see tellPositionOrNot), so
   // only this float has to be tested against all textblocks.
   if (wasAllocated (vloat->generatingBlock)) {
      // TODO This (and similar code) is not very efficient.
      for (lout::container::typed::Iterator<TypedPointer <Textblock> > it =
              tbInfosByTextblock->iterator (); it.hasNext (); ) {
         TypedPointer <Textblock> *key = it.getNext ();
         Textblock *textblock = key->getTypedValue();

         if (textblock != vloat->generatingBlock && wasAllocated (textblock)) {
            Allocation *tba = getAllocation (textblock);
            Allocation *gba = getAllocation (vloat->generatingBlock);
            int tby1 = tba->y;
            int tby2 = tba->y + tba->ascent + tba->descent;

            int flh =
               vloat->dirty ? 0 : vloat->size.ascent + vloat->size.descent;
            int y1old = gba->y + oldY, y2old = y1old + flh;
            int y1new = gba->y + vloat->yReal, y2new = y1new + flh;

            bool covered =
               (y2old > tby1 && y1old < tby2) || (y2new > tby1 && y1new < tby2);

            if (covered) {
               int yTextblock = gba->y + min (oldY, vloat->yReal) - tba->y;
               textblock->borderChanged (yTextblock, vloat->widget);
            }
         }
      }
   }
}

void OutOfFlowMgr::getFloatsLists (Float *vloat, SortedFloatsVector **listSame,
                                   SortedFloatsVector **listOpp)
{
   TBInfo *tbInfo = registerCaller (vloat->generatingBlock);
      
   switch (vloat->widget->getStyle()->vloat) {
   case FLOAT_LEFT:
      if (wasAllocated (vloat->generatingBlock)) {
         if (listSame) *listSame = leftFloatsCB;
         if (listOpp) *listOpp = rightFloatsCB;
      } else {
         if (listSame) *listSame = tbInfo->leftFloatsGB;
         if (listOpp) *listOpp = tbInfo->rightFloatsGB;
      }
      break;

   case FLOAT_RIGHT:
      if (wasAllocated (vloat->generatingBlock)) {
         if (listSame) *listSame = rightFloatsCB;
         if (listOpp) *listOpp = leftFloatsCB;
      } else {
         if (listSame) *listSame = tbInfo->rightFloatsGB;
         if (listOpp) *listOpp = tbInfo->leftFloatsGB;
      }
      break;

   default:
      assertNotReached();
   }
}

void OutOfFlowMgr::getSize (int cbWidth, int cbHeight,
                            int *oofWidth, int *oofHeight)
{
   // CbWidth and cbHeight *do* contain padding, border, and
   // margin. See call in dw::Textblock::sizeRequest. (Notice that
   // this has changed from an earlier version.)

   // Also notice that Float::y includes margins etc.

   // TODO Is it correct to add padding, border, and margin to the
   // containing block? Check CSS spec.

   //printf ("[%p] GET_SIZE (%d / %d floats)...\n",
   //        containingBlock, leftFloatsCB->size(), rightFloatsCB->size());

   *oofWidth = cbWidth; /* This (or "<=" instead of "=") should be
                           the case for floats. */

   int oofHeightLeft = getFloatsSize (leftFloatsCB);
   int oofHeightRight = getFloatsSize (rightFloatsCB);
   *oofHeight = max (oofHeightLeft, oofHeightRight);

   //printf ("   => %d x %d => %d x %d (%d / %d)\n",
   //        cbWidth, cbHeight, *oofWidth, *oofHeight,
   //        oofHeightLeft, oofHeightRight);
}

int OutOfFlowMgr::getFloatsSize (SortedFloatsVector *list)
{
   int height = containingBlock->getStyle()->boxDiffHeight();

   // Idea for a faster implementation: find the last float; this
   // should be the relevant one, since the list is sorted.
   for (int i = 0; i < list->size(); i++) {
      Float *vloat = list->get(i);
      ensureFloatSize (vloat);

      // Notice that all positions are relative to the generating
      // block, but we need them relative to the containing block.

      // Position of generating block, relative to containing
      // block. Greater or equal than 0, so dealing with 0 when it
      // cannot yet be calculated is safe. (No distiction whether it
      // is defined or not is necessary.)

      int yGBinCB;

      if (vloat->generatingBlock == containingBlock)
         // Simplest case: the generator is the container.
         yGBinCB = 0;
      else {
         if (wasAllocated (containingBlock)) {
            if (wasAllocated (vloat->generatingBlock))
               // Simple case: both containing block and generating
               // block are defined.
               yGBinCB = getAllocation(vloat->generatingBlock)->y
                  - containingBlock->getAllocation()->y;
            else
               // Generating block not yet allocation; the next
               // allocation will, when necessary, trigger
               // sizeRequest. (TODO: Is this really the case?)
               yGBinCB = 0;
         } else
            // Nothing can be done now, but the next allocation
            // will trigger sizeAllocate. (TODO: Is this really the
            // case?)
            yGBinCB = 0;
      }
      
      height =
         max (height,
              yGBinCB + vloat->yReal + vloat->size.ascent + vloat->size.descent
              + containingBlock->getStyle()->boxRestHeight());
      //printf ("   float %d: (%d + %d) + (%d + %d + %d) => %d\n",
      //        i, yGBinCB, vloat->yReal, vloat->size.ascent,
      //        vloat->size.descent,
      //        containingBlock->getStyle()->boxRestHeight(), height);
   }

   return height;
}

void OutOfFlowMgr::getExtremes (int cbMinWidth, int cbMaxWidth,
                                int *oofMinWidth, int *oofMaxWidth)
{
   *oofMinWidth = *oofMaxWidth = 0;
   accumExtremes (leftFloatsCB, oofMinWidth, oofMaxWidth);
   accumExtremes (rightFloatsCB, oofMinWidth, oofMaxWidth);
}

void OutOfFlowMgr::accumExtremes (SortedFloatsVector *list, int *oofMinWidth,
                                  int *oofMaxWidth)
{
   // Idea for a faster implementation: use incremental resizing?

   for (int i = 0; i < list->size(); i++) {
      Float *vloat = list->get(i);

      // Difference between generating block and to containing block,
      // sum on both sides. Greater or equal than 0, so dealing with 0
      // when it cannot yet be calculated is safe. (No distiction
      // whether it is defined or not is necessary.)
      int borderDiff;

      if (vloat->generatingBlock == containingBlock)
         // Simplest case: the generator is the container.
         borderDiff = 0;
      else {
         if (wasAllocated (containingBlock)) {
            if (wasAllocated (vloat->generatingBlock))
               // Simple case: both containing block and generating
               // block are defined.
               borderDiff = getAllocation(containingBlock)->width -
                  getAllocation(vloat->generatingBlock)->width;
            else
               // Generating block not yet allocation; the next
               // allocation will, when necessary, trigger
               // getExtremes. (TODO: Is this really the case?)
               borderDiff = 0;
         } else
            // Nothing can be done now, but the next allocation will
            // trigger getExtremes. (TODO: Is this really the case?)
            borderDiff = 0;
      }

      Extremes extr;
      vloat->widget->getExtremes (&extr);

      *oofMinWidth = max (*oofMinWidth, extr.minWidth + borderDiff);
      *oofMaxWidth = max (*oofMaxWidth, extr.maxWidth + borderDiff);
   }
}

OutOfFlowMgr::TBInfo *OutOfFlowMgr::registerCaller (Textblock *textblock)
{
   TypedPointer<Textblock> key (textblock);
   TBInfo *tbInfo = tbInfosByTextblock->get (&key);
   if (tbInfo == NULL) {
      tbInfo = new TBInfo (this, textblock);
      tbInfo->wasAllocated = false;
      tbInfo->index = tbInfos->size();

      tbInfos->put (tbInfo);
      tbInfosByTextblock->put (new TypedPointer<Textblock> (textblock), tbInfo);
   }

   return tbInfo;
}
   
/**
 * Get the left border for the vertical position of *y*, for a height
 * of *h", based on floats; relative to the allocation of the calling
 * textblock.
 *
 * The border includes marging/border/padding of the calling textblock
 * but is 0 if there is no float, so a caller should also consider
 * other borders.
 */
int OutOfFlowMgr::getLeftBorder (Textblock *textblock, int y, int h,
                                 Textblock *lastGB, int lastExtIndex)
{
   int b = getBorder (textblock, LEFT, y, h, lastGB, lastExtIndex);
   //printf ("getLeftBorder (%p, %d, %d) => %d\n", textblock, y, h, b);
   return b;
}

/**
 * Get the right border for the vertical position of *y*, for a height
 * of *h", based on floats.
 *
 * See also getLeftBorder(int, int);
 */
int OutOfFlowMgr::getRightBorder (Textblock *textblock, int y, int h,
                                  Textblock *lastGB, int lastExtIndex)
{
   int b = getBorder (textblock, RIGHT, y, h, lastGB, lastExtIndex);
   //printf ("getRightBorder (%p, %d, %d) => %d\n", textblock, y, h, b);
   return b;
}

int OutOfFlowMgr::getBorder (Textblock *textblock, Side side, int y, int h,
                             Textblock *lastGB, int lastExtIndex)
{
   //printf ("[%p] GET_BORDER (%p (allocated: %s), %s, %d, %d, %p, %d)\n",
   //        containingBlock, textblock,
   //        wasAllocated (textblock) ? "true" : "false",
   //        side == LEFT ? "LEFT" : "RIGHT", y, h, lastGB, lastExtIndex);

   SortedFloatsVector *list = getFloatsListForTextblock (textblock, side);

   //printf ("   searching in list:\n");
   //for (int i = 0; i < list->size(); i++) {
   //   printf ("      %d: %s\n", i, list->get(i)->toString());
   //   //printf ("         (widget at (%d, %d))\n",
   //   //        list->get(i)->widget->getAllocation()->x,
   //   //        list->get(i)->widget->getAllocation()->y);
   //}

   int first = list->findFirst (textblock, y, h, lastGB, lastExtIndex);

   //printf ("   first = %d\n", first);

   if (first == -1)
      // No float.
      return 0;
   else {
      // It is not sufficient to find the first float, since a line
      // (with height h) may cover the region of multiple float, of
      // which the widest has to be choosen.
      int border = 0;
      bool covers = true;
      // TODO Also check against lastGB and lastExtIndex
      for (int i = first; covers && i < list->size(); i++) {
         Float *vloat = list->get(i);
         covers = vloat->covers (textblock, y, h);
         //printf ("   float %d: %s; covers? %s.\n",
         //        i, vloat->toString(), covers ? "yes" : "no");

         if (covers) {
            int borderDiff = getBorderDiff (textblock, vloat, side);
            int borderIn = side == LEFT ?
               vloat->generatingBlock->getStyle()->boxOffsetX() :
               vloat->generatingBlock->getStyle()->boxRestWidth();
            border = max (border, vloat->size.width + borderIn + borderDiff);
            //printf ("   => border = %d\n", border);
         }
      }

      return border;
   }
}


OutOfFlowMgr::SortedFloatsVector *OutOfFlowMgr::getFloatsListForTextblock
   (Textblock *textblock, Side side)
{
   TBInfo *tbInfo = registerCaller (textblock);
   if (wasAllocated (textblock))
      return side == LEFT ? leftFloatsCB : rightFloatsCB;
   else
      return side == LEFT ? tbInfo->leftFloatsGB : tbInfo->rightFloatsGB;
}


bool OutOfFlowMgr::hasFloatLeft (Textblock *textblock, int y, int h,
                                 Textblock *lastGB, int lastExtIndex)
{
   return hasFloat (textblock, LEFT, y, h, lastGB, lastExtIndex);
}

bool OutOfFlowMgr::hasFloatRight (Textblock *textblock, int y, int h,
                                  Textblock *lastGB, int lastExtIndex)
{
   return hasFloat (textblock, RIGHT, y, h, lastGB, lastExtIndex);
}

bool OutOfFlowMgr::hasFloat (Textblock *textblock, Side side, int y, int h,
                             Textblock *lastGB, int lastExtIndex)
{
   //printf ("[%p] hasFloat (%p, %s, %d, %d, %p, %d)\n",
   //        containingBlock, textblock, side == LEFT ? "LEFT" : "RIGHT", y, h,
   //        lastGB, lastExtIndex);
   SortedFloatsVector *list = getFloatsListForTextblock(textblock, side);
   return list->findFirst (textblock, y, h, lastGB, lastExtIndex) != -1;
}

void OutOfFlowMgr::ensureFloatSize (Float *vloat)
{
   if (vloat->dirty) {
      // TODO Ugly. Soon to be replaced by cleaner code? See also
      // comment in Textblock::calcWidgetSize.
      if (vloat->widget->usesHints ()) {
         if (isAbsLength (vloat->widget->getStyle()->width))
            vloat->widget->setWidth
               (absLengthVal (vloat->widget->getStyle()->width));
         else if (isPerLength (vloat->widget->getStyle()->width))
            vloat->widget->setWidth
               (containingBlock->getAvailWidth()
                * perLengthVal (vloat->widget->getStyle()->width));
      }

      // This is a bit hackish: We first request the size, then set
      // the available width (also considering the one of the
      // containing block, and the extremes of the float), then
      // request the size again, which may of course have a different
      // result. This is a fix for the bug:
      //
      //    Text in floats, which are wider because of an image, are
      //    broken at a too narrow width. Reproduce:
      //    test/floats2.html. After the image has been loaded, the
      //    text "Some text in a float." should not be broken
      //    anymore.
      //
      // If the call of setWidth not is neccessary, the second call
      // will read the size from the cache, so no redundant
      // calculation is necessary.

      // Furthermore, extremes are considered; especially, floats are too
      // wide, sometimes.
      Extremes extremes;
      vloat->widget->getExtremes (&extremes);

      vloat->widget->sizeRequest (&vloat->size);

      // Set width  ...
      int width = vloat->size.width;
      // Consider the available width of the containing block (when set):
      if (width > containingBlock->getAvailWidth())
         width = containingBlock->getAvailWidth();
      // Finally, consider extremes (as described above).
      if (width < extremes.minWidth)
          width = extremes.minWidth;
      if (width > extremes.maxWidth)
         width = extremes.maxWidth;
          
      vloat->widget->setWidth (width);
      vloat->widget->sizeRequest (&vloat->size);
      
      //printf ("   float %p (%s %p): %d x (%d + %d)\n",
      //        vloat, vloat->widget->getClassName(), vloat->widget,
      //        vloat->size.width, vloat->size.ascent, vloat->size.descent);
          
      vloat->dirty = false;      
   }
}

/**
 * Return the between generator and calling textblock. (TODO Exact
 * definition. See getBorder(), where it is used.)
 *
 * Assumes that the position can be determined (getYWidget() returns true).
 */
int OutOfFlowMgr::getBorderDiff (Textblock *textblock, Float *vloat, Side side)
{
   if (textblock == vloat->generatingBlock)
      return 0;
   else {
      assert (wasAllocated (textblock) &&
              wasAllocated (vloat->generatingBlock));
      
      switch (side) {
      case LEFT:
         return getAllocation(vloat->generatingBlock)->x
            - getAllocation(textblock)->x;

      case RIGHT:
         return
            getAllocation(textblock)->x + getAllocation(textblock)->width
            - (getAllocation(vloat->generatingBlock)->x +
               min (getAllocation(vloat->generatingBlock)->width,
                    vloat->generatingBlock->getAvailWidth()));

      default:
         assertNotReached();
         return 0;
      }
   }
}

// TODO Latest change: Check also Textblock::borderChanged: looks OK,
// but the comment ("... (i) with canvas coordinates ...") looks wrong
// (and looks as having always been wrong).

// Another issue: does it make sense to call Textblock::borderChanged
// for generators, when the respective widgets have not been called
// yet?

} // namespace dw
