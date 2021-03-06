/*
 *    Copyright 2012, 2013 Thomas Schöps
 *    Copyright 2012-2015 Kai Pastor
 *
 *    This file is part of OpenOrienteering.
 *
 *    OpenOrienteering is free software: you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation, either version 3 of the License, or
 *    (at your option) any later version.
 *
 *    OpenOrienteering is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with OpenOrienteering.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "renderable.h"

#include <QPainter>
#include <qmath.h>

#include "core/image_transparency_fixup.h"
#include "core/map_color.h"
#include "core/map.h"
#include "core/objects/object.h"
#include "core/symbols/symbol.h"
#include "util/util.h"

#if defined(Q_OS_ANDROID) && defined(QT_PRINTSUPPORT_LIB)
static_assert(false, "This file needs to be modified for correct printing on Android");
#endif

/* 
 * The macro MAPPER_OVERPRINTING_CORRECTION allows to select different
 * implementations of spot color overprinting simulation correction towards
 * the appearance of colors in normal (non-spot color) output.
 * 
 *         -1:  Mapper 0.5.0 correction.
 *              Results in undesired brightening when overprinting halftones.
 * 
 *          0:  No correction. Plain multiply spot color composition.
 *              Results in undesired green from 100% blue on 100% yellow.
 * 
 *          1:  Weak correction. Blends the normal output over the 
 *              overprinting simulation with an alpha of 0.125.
 * 
 *          2:  Middle correction. Blends the normal output over the 
 *              overprinting simulation with an alpha of 0.25.
 * 
 *          3:  Strong correction. Blends the normal output over the 
 *              overprinting simulation with an alpha of 0.5.
 * 
 * Options 3 and 2 seem to give the best results. Output from option 3 is quite
 * similar to option -1 (Mapper 0.5.0), but without the undesired brightening.
 * 
 * Options 1..3 work only as long as the color set and the symbol set are
 * defined in a way that the raw overprinting simulation output and the normal
 * output do not differ significantly.
 */
#ifndef MAPPER_OVERPRINTING_CORRECTION
	
	// Default: [new] middle correction
	#define MAPPER_OVERPRINTING_CORRECTION 2
	
#endif



// ### Renderable ###

Renderable::~Renderable()
{
	; // nothing, not inlined
}



// ### SharedRenderables ###

SharedRenderables::~SharedRenderables()
{
	deleteRenderables();
}

void SharedRenderables::deleteRenderables()
{
	for (iterator renderables = begin(); renderables != end(); )
	{
		for (Renderable* renderable : renderables->second)
		{
			delete renderable;
		}
		renderables->second.clear();
		if (renderables->first.clip_path != NULL)
		{
			iterator it = renderables;
			++renderables;
			erase(it);
		}
		else
			++renderables;
	}
}

void SharedRenderables::compact()
{
	for (iterator renderables = begin(); renderables != end(); )
	{
		if (renderables->second.size() == 0)
		{
			iterator it = renderables;
			++renderables;
			erase(it);
		}
		else
			++renderables;
	}
}


// ### ObjectRenderables ###

ObjectRenderables::ObjectRenderables(Object& object)
: extent(object.extent),
  clip_path(NULL)
{
	;
}

ObjectRenderables::~ObjectRenderables()
{
	;
}

void ObjectRenderables::draw(const QColor& color, QPainter* painter, const RenderConfig& config) const
{
	QPainterPath initial_clip = painter->clipPath();
	const QPainterPath* current_clip = nullptr;
	
	painter->save();
	
	for (const auto& color_renderables : *this)
	{
		for (const auto& config_renderables : *color_renderables.second)
		{
			// Render the renderables
			const PainterConfig& state = config_renderables.first;
			if (!state.activate(painter, current_clip, config, color, initial_clip))
				continue;
			
			for (Renderable* renderable : config_renderables.second)
			{
				if (renderable->intersects(config.bounding_box))
				{
					renderable->render(*painter, config);
				}
			}
		}
	}
	
	painter->restore();
}

void ObjectRenderables::setClipPath(const QPainterPath* path)
{
	clip_path = path;
}

void ObjectRenderables::insertRenderable(Renderable* r, PainterConfig state)
{
	SharedRenderables::Pointer& container(operator[](state.color_priority));
	if (!container)
		container = new SharedRenderables();
	container->operator[](state).push_back(r);
	if (clip_path == NULL)
	{
		if (extent.isValid())
			rectInclude(extent, r->getExtent());
		else
			extent = r->getExtent();
	}
}

void ObjectRenderables::clear()
{
	for (auto& renderables : *this)
	{
		renderables.second->clear();
	}
}

void ObjectRenderables::takeRenderables()
{
	for (iterator color = begin(); color != end(); ++color)
	{
		SharedRenderables* new_container = new SharedRenderables();
		
		// Pre-allocate as much space as in the original container
		for (SharedRenderables::const_iterator it = color->second->begin(); it != color->second->end(); ++it)
		{
			new_container->operator[](it->first).reserve(it->second.size());
		}
		color->second = new_container;
	}
}

void ObjectRenderables::deleteRenderables()
{
	for (const_iterator color = begin(); color != end(); ++color)
	{
		color->second->deleteRenderables();
	}
}



// ### MapRenderables ###

void MapRenderables::ObjectDeleter::operator()(Object* object) const
{
	renderables.removeRenderablesOfObject(object, false);
	delete object;
}

MapRenderables::MapRenderables(Map* map)
 : map(map)
{
	; // nothing
}

void MapRenderables::draw(QPainter *painter, const RenderConfig &config) const
{
	// TODO: improve performance by using some spatial acceleration structure?
	
#ifdef Q_OS_ANDROID
	const qreal min_dimension = 1.0/config.scaling;
#endif
	
	QPainterPath initial_clip = painter->clipPath();
	const QPainterPath* current_clip = NULL;
	
	painter->save();
	const_reverse_iterator end_of_colors = rend();
	const_reverse_iterator color = rbegin();
	while (color != end_of_colors && color->first >= map->getNumColors())
	{
		++color;
	}
	for (; color != end_of_colors; ++color)
	{
		if ( config.testFlag(RenderConfig::RequireSpotColor) &&
		     (color->first < 0 || map->getColor(color->first)->getSpotColorMethod() == MapColor::UndefinedMethod) )
		{
			continue;
		}
		
		ObjectRenderablesMap::const_iterator end_of_objects = color->second.end();
		for (ObjectRenderablesMap::const_iterator object = color->second.begin(); object != end_of_objects; ++object)
		{
			// Settings check
			const Symbol* symbol = object->first->getSymbol();
			if (!config.testFlag(RenderConfig::HelperSymbols) && symbol->isHelperSymbol())
				continue;
			if (symbol->isHidden())
				continue;
			
			if (!object->first->getExtent().intersects(config.bounding_box))
				continue;
			
			SharedRenderables::const_iterator it_end = object->second->end();
			for (SharedRenderables::const_iterator it = object->second->begin(); it != it_end; ++it)
			{
				// Render the renderables
				const PainterConfig& state = it->first;
				const MapColor* map_color = map->getColor(state.color_priority);
				if (!map_color)
				{
					Q_ASSERT(state.color_priority == MapColor::Reserved);
					continue; // in release build
				}
				QColor color = *map_color;
				if (state.color_priority >= 0 && map_color->getOpacity() < 1.0)
					color.setAlphaF(map_color->getOpacity());
				if (!state.activate(painter, current_clip, config, color, initial_clip))
				    continue;
				
				for (Renderable* renderable : it->second)
				{
#ifdef Q_OS_ANDROID
					const QRectF& extent = renderable->getExtent();
					if (extent.width() < min_dimension && extent.height() < min_dimension)
						continue;
#endif
					if (renderable->intersects(config.bounding_box))
					{
						renderable->render(*painter, config);
					}
				}
				
			} // each common render attributes
			
		} // each object
		
	} // each map color
	
	painter->restore();
}

void MapRenderables::drawOverprintingSimulation(QPainter* painter, const RenderConfig& config) const
{
	// NOTE: painter must be a QPainter on a QImage of Format_ARGB32_Premultiplied.
	QImage* image = static_cast<QImage*>(painter->device());
	ImageTransparencyFixup image_fixup(image);
	
	QPainter::RenderHints hints = painter->renderHints();
	QTransform t = painter->worldTransform();
	painter->save();
	
	painter->resetTransform();
	painter->setCompositionMode(QPainter::CompositionMode_Multiply); // Alternative: CompositionMode_Darken
	
	QImage separation(image->size(), QImage::Format_ARGB32_Premultiplied);
	
	for (Map::ColorVector::reverse_iterator map_color = map->color_set->colors.rbegin();
	     map_color != map->color_set->colors.rend();
	     map_color++)
	{
		if ((*map_color)->getSpotColorMethod() == MapColor::SpotColor)
		{
			separation.fill((Qt::GlobalColor)Qt::transparent);
			
			// Collect all halftones and knockouts of a single color
			QPainter p(&separation);
			p.setRenderHints(hints);
			p.setWorldTransform(t, false);
			drawColorSeparation(&p, config, *map_color, true);
			p.end();
			
			// Add this separation to the composition with multiplication.
			painter->setCompositionMode(QPainter::CompositionMode_Multiply);
			painter->drawImage(0, 0, separation);
			image_fixup();
			
#if MAPPER_OVERPRINTING_CORRECTION == -1
			// Add some opacity to the multiplication, but not for black,
			// since halftones (i.e. grey) might unduly lighten the composition.
			if (static_cast<QRgb>(**map_color) != 0xff000000)
			{
				// FIXME: Implement this for Format_ARGB32_Premultiplied,
				//        if efficiently possible.
				QImage copy = separation.convertToFormat(QImage::Format_ARGB32);
				QRgb* dest = (QRgb*)copy.bits();
				const QRgb* dest_end = dest + copy.byteCount() / sizeof(QRgb);
				for (QRgb* px = dest; px < dest_end; ++px)
				{
					const unsigned int alpha = qAlpha(*px) * ((255-qGray(*px)) << 16) & 0xff000000;
					*px = alpha | (*px & 0xffffff);
				}
				painter->setCompositionMode(QPainter::CompositionMode_SourceOver);
				painter->drawImage(0, 0, copy);
			}
#endif
		}
	}
	
	painter->setCompositionMode(QPainter::CompositionMode_SourceOver);
	
#if MAPPER_OVERPRINTING_CORRECTION > 0
	separation.fill((Qt::GlobalColor)Qt::transparent);
	QPainter p(&separation);
	p.setRenderHints(hints);
	p.setWorldTransform(t, false);
	RenderConfig config_copy = config;
	config_copy.options |= RenderConfig::RequireSpotColor;
	draw(&p, config_copy);
	p.end();
	QRgb* dest = (QRgb*)separation.bits();
	const QRgb* dest_end = dest + separation.byteCount() / sizeof(QRgb);
	for (QRgb* px = dest; px < dest_end; ++px)
	{
		/* Each pixel is a premultipled RGBA, so the alpha value is adjusted
		 * by applying the same factor to all 4 channels (bytes).
		 * Implemented by bitwise operators for efficiency.
		 */
#if MAPPER_OVERPRINTING_CORRECTION == 1
		*px = (*px >> 3) & 0x1f1f1f1f;
#elif MAPPER_OVERPRINTING_CORRECTION == 2
		*px = (*px >> 2) & 0x3f3f3f3f;
#else /* MAPPER_OVERPRINTING_CORRECTION == 3 or stronger */
		*px = (*px >> 1) & 0x7f7f7f7f;
#endif
	}
	painter->drawImage(0, 0, separation);
#endif
	
	if (config.testFlag(RenderConfig::Screen))
	{
		static MapColor reserved_color(MapColor::Reserved);
		drawColorSeparation(painter, config, &reserved_color, true);
	}
	
	painter->restore();
}

void MapRenderables::drawColorSeparation(QPainter* painter, const RenderConfig& config, const MapColor* separation, bool use_color) const
{
	painter->save();
	
	const QPainterPath initial_clip(painter->clipPath());
	const QPainterPath* current_clip = nullptr;
	
	// As soon as the spot color is actually used for drawing (i.e. drawing_started = true),
	// we need to take care of knockouts.
	bool drawing_started = false;
	
	// For each pair of color priority and its renderables collection...
	const_reverse_iterator end_of_colors = rend();
	const_reverse_iterator color = rbegin();
	while (color != end_of_colors && color->first >= map->getNumColors())
	{
		++color;
	}
	for (; color != end_of_colors; ++color)
	{
		SpotColorComponent drawing_color(map->getColor(color->first), 1.0f);
		
		// Check whether the current color [priority] applies to the current separation.
		if (color->first > MapColor::Reserved)
		{
			if (separation->getPriority() == MapColor::Reserved)
			{
				// Don't process regular colors for the "Reserved" separation.
				continue;
			}
			
			switch (drawing_color.spot_color->getSpotColorMethod())
			{
				case MapColor::UndefinedMethod:
					continue;
				
				case MapColor::SpotColor:
					if (drawing_color.spot_color == separation)
					{
						; // okay
					}
					else if (drawing_started && drawing_color.spot_color->getKnockout())
					{
						drawing_color.factor = 0.0f;
					}
					else
					{
						continue;
					}
					break;
				
				case MapColor::CustomColor:
				{
					// First, check if the renderables draw color to this separation
					// TODO: Use an efficient data structure to avoid reiterating each time a separation is drawn
					const SpotColorComponents& components = drawing_color.spot_color->getComponents();
					for ( SpotColorComponents::const_iterator component = components.begin(), c_end = components.end();
						component != c_end;
						++component )
					{
						if (component->spot_color == separation)
						{
							// The renderables do draw the current spot color
							drawing_color = *component;
							break;
						}
					}
					if (drawing_color.spot_color != separation)
					{
						// If the renderables do not explicitly draw color to this separation,
						// check if they need a knockout.
						if (drawing_started && drawing_color.spot_color->getKnockout())
						{
							drawing_color = SpotColorComponent(separation, 0.0f);
						}
						else
						{
							continue;
						}
					}
					break;
				}
				
				default:
					Q_ASSERT(false); // in development builds
					continue;        // in release build
			}
		}
		else if (separation->getPriority() == MapColor::Reserved)
		{
			if (color->first == MapColor::Registration)
				continue; // treated per spot color
			else if (color->first == MapColor::Reserved)
				continue; // never drawn
			else if (drawing_color.spot_color == NULL)
			{
				Q_ASSERT(!"Invalid reserved color!");                // in development build
				drawing_color.spot_color = Map::getUndefinedColor(); // in release build
			}
			painter->setRenderHint(QPainter::Antialiasing, true);
		}
		else if (color->first == MapColor::Registration)
		{
			// Draw Registration Black as fulltone of regular spot color
			drawing_color.spot_color = separation;
		}
		else
		{
			// Don't draw reserved color in regular separation.
			continue;
		}
		
		// For each pair of object and its renderables [states] for a particular map color...
		ObjectRenderablesMap::const_iterator end_of_objects = color->second.end();
		for (ObjectRenderablesMap::const_iterator object = color->second.begin(); object != end_of_objects; ++object)
		{
			// Check whether the symbol and object is to be drawn at all.
			const Symbol* symbol = object->first->getSymbol();
			if (!config.testFlag(RenderConfig::HelperSymbols) && symbol->isHelperSymbol())
				continue;
			if (symbol->isHidden())
				continue;
			
			if (!object->first->getExtent().intersects(config.bounding_box))
				continue;
			
			// For each pair of common rendering attributes and collection of renderables...
			SharedRenderables::const_iterator it_end = object->second->end();
			for (SharedRenderables::const_iterator it = object->second->begin(); it != it_end; ++it)
			{
				const PainterConfig& state = it->first;
				
				QColor color = *drawing_color.spot_color;
				bool drawing = (drawing_color.factor >= 0.0005f);
				if (!drawing)
				{
					if (!drawing_started)
						continue;
					color = Qt::white;
				}
				else if (use_color)
				{
					qreal c, m, y, k;
					color.getCmykF(&c, &m, &y, &k);
					color.setCmykF(c*drawing_color.factor, m*drawing_color.factor, y*drawing_color.factor, k*drawing_color.factor, 1.0f);
				}
				else
				{
					color.setCmykF(0.0f, 0.0f, 0.0f, drawing_color.factor, 1.0f);
				}
				
				if (!state.activate(painter, current_clip, config, color, initial_clip))
					continue;
				
				// For each renderable that uses the current painter configuration...
				// Render the renderable
				for (Renderable* renderable : it->second)
				{
					if (renderable->intersects(config.bounding_box))
					{
						renderable->render(*painter, config);
						drawing_started |= drawing;
					}
				}
				
			} // each common render attributes
			
		} // each object
		
	} // each map color
	
	painter->restore();
}

void MapRenderables::insertRenderablesOfObject(const Object* object)
{
	ObjectRenderables::const_iterator end_of_colors = object->renderables().end();
	ObjectRenderables::const_iterator color = object->renderables().begin();
	for (; color != end_of_colors; ++color)
	{
		operator[](color->first)[object] = color->second;
	}
}

void MapRenderables::removeRenderablesOfObject(const Object* object, bool mark_area_as_dirty)
{
	const_iterator end_of_colors = end();
	for (iterator color = begin(); color != end_of_colors; ++color)
	{
		ObjectRenderablesMap::iterator obj = color->second.find(object);
		if (obj != color->second.end())
		{
			if (mark_area_as_dirty)
			{
				// We don't want to loop over every dot in an area ...
				QRectF extent = object->getExtent();
				if (!extent.isValid())
				{
					// ... because here it gets expensive
					for (SharedRenderables::const_iterator renderables = obj->second->begin(); renderables != obj->second->end(); ++renderables)
					{
						for (RenderableVector::const_iterator renderable = renderables->second.begin(); renderable != renderables->second.end(); ++renderable)
						{
							extent = extent.isValid() ? extent.united((*renderable)->getExtent()) : (*renderable)->getExtent();
						}
					}
				}
				map->setObjectAreaDirty(extent);
			}
			
			color->second.erase(obj);
		}
	}
}

void MapRenderables::clear(bool mark_area_as_dirty)
{
	if (mark_area_as_dirty)
	{
		const_iterator end_of_colors = end();
		for (const_iterator color = begin(); color != end_of_colors; ++color)
		{
			ObjectRenderablesMap::const_iterator end_of_objects = color->second.end();
			for (ObjectRenderablesMap::const_iterator object = color->second.begin(); object != end_of_objects; ++object)
			{
				for (SharedRenderables::const_iterator renderables = object->second->begin(); renderables != object->second->end(); ++renderables)
				{
					for (RenderableVector::const_iterator renderable = renderables->second.begin(); renderable != renderables->second.end(); ++renderable)
					{
						map->setObjectAreaDirty((*renderable)->getExtent());
					}
				}
			}
		}
	}
	std::map<int, ObjectRenderablesMap>::clear();
}

// ### PainterConfig ###

namespace {
	inline
	QColor highlightedColor(const QColor& original)
	{
		const int highlight_alpha = 255;
		
		if (original.value() > 127)
		{
			const float factor = 0.35f;
			return QColor(factor * original.red(), factor * original.green(), factor * original.blue(), highlight_alpha);
		}
		else
		{
			const float factor = 0.15f;
			return QColor(255 - factor * (255 - original.red()), 255 - factor * (255 - original.green()), 255 - factor * (255 - original.blue()), highlight_alpha);
		}
	}
}

bool PainterConfig::activate(QPainter* painter, const QPainterPath*& current_clip, const RenderConfig& config, const QColor& color, const QPainterPath& initial_clip) const
{
	if (current_clip != clip_path)
	{
		if (initial_clip.isEmpty())
		{
			if (clip_path)
				painter->setClipPath(*clip_path, Qt::ReplaceClip);
			else
				painter->setClipPath(initial_clip, Qt::NoClip);
		}
		else if (clip_path)
		{
			/* This used to be a workaround for a Qt::IntersectClip problem
			 * with Windows and Mac printers (cf. [tickets:#196]), and 
			 * with Linux PDF export (cf. [tickets:#225]).
			 * But it seems to be faster in general.
			 */
			QPainterPath merged = initial_clip.intersected(*clip_path);
			if (merged.isEmpty())
				return false; // outside of initial clip
			painter->setClipPath(merged, Qt::ReplaceClip);
		}
		else
		{
			painter->setClipPath(initial_clip, Qt::ReplaceClip);
		}
		current_clip = clip_path;
	}
	
	qreal actual_pen_width = pen_width;
	
	if (color_priority < 0 && color_priority != MapColor::Registration)
	{
		if (color_priority == MapColor::Reserved)
			return false;
		
		if (!config.testFlag(RenderConfig::DisableAntialiasing))
		{
			// this is not undone here anywhere as it should apply to 
			// all special symbols and these are always painted last
			painter->setRenderHint(QPainter::Antialiasing, true);
		}
		
		actual_pen_width /= config.scaling;
	}
	else if (config.testFlag(RenderConfig::DisableAntialiasing))
	{
		painter->setRenderHint(QPainter::Antialiasing, false);
		painter->setRenderHint(QPainter::TextAntialiasing, false);
	}
	
	QBrush brush(config.testFlag(RenderConfig::Highlighted) ? highlightedColor(color) : color);
	if (mode == PainterConfig::PenOnly)
	{
#ifdef Q_OS_ANDROID
		if (pen_width * config.scaling < 0.1)
			return false;
#endif
		if (config.testFlag(RenderConfig::ForceMinSize) && pen_width * config.scaling <= 1.0)
			actual_pen_width = 0.0; // Forces cosmetic pen
		painter->setPen(QPen(brush, actual_pen_width));
		painter->setBrush(QBrush(Qt::NoBrush));
	}
	else if (mode == PainterConfig::BrushOnly)
	{
		painter->setPen(QPen(Qt::NoPen));
		painter->setBrush(brush);
	}
	
	painter->setOpacity(config.opacity);
	
	return true;
}
