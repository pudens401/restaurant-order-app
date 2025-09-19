const express = require("express");
const router = express.Router();
const MenuItem = require("../models/MenuItem");
const Order = require("../models/Order");

// Home route - redirect to menu
router.get("/", (req, res) => {
  res.redirect("/menu");
});

// Initialize cart in session if it doesn't exist
function initializeCart(req) {
  if (!req.session.cart) {
    req.session.cart = [];
  }
}

// Calculate cart total
function calculateCartTotal(cart) {
  return cart.reduce((total, item) => total + (item.price * item.quantity), 0);
}

// ---------- MENU ROUTES ----------

// Display menu items for customers
router.get("/menu", async (req, res) => {
  try {
    initializeCart(req);
    const menuItems = await MenuItem.find().sort({ category: 1, name: 1 });
    
    // Group items by category
    const groupedItems = {};
    menuItems.forEach(item => {
      const category = item.category || "General";
      if (!groupedItems[category]) {
        groupedItems[category] = [];
      }
      groupedItems[category].push(item);
    });

    res.render("guest/menu", { 
      groupedItems,
      cartCount: req.session.cart ? req.session.cart.reduce((sum, item) => sum + item.quantity, 0) : 0
    });
  } catch (error) {
    console.error("Error loading menu:", error);
    res.status(500).send("Error loading menu");
  }
});

// ---------- CART ROUTES ----------

// Add item to cart
router.post("/cart/add", async (req, res) => {
  try {
    const { menuItemId, quantity = 1 } = req.body;
    initializeCart(req);

    const menuItem = await MenuItem.findById(menuItemId);
    if (!menuItem) {
      return res.status(404).json({ error: "Menu item not found" });
    }

    // Check if item already exists in cart
    const existingItem = req.session.cart.find(item => item.menuItemId === menuItemId);
    
    if (existingItem) {
      existingItem.quantity += parseInt(quantity);
    } else {
      req.session.cart.push({
        menuItemId: menuItem._id.toString(),
        name: menuItem.name,
        price: menuItem.price,
        photo: menuItem.photo,
        quantity: parseInt(quantity)
      });
    }

    res.json({ 
      success: true, 
      cartCount: req.session.cart.reduce((sum, item) => sum + item.quantity, 0) 
    });
  } catch (error) {
    console.error("Error adding to cart:", error);
    res.status(500).json({ error: "Error adding to cart" });
  }
});

// View cart
router.get("/cart", (req, res) => {
  initializeCart(req);
  const total = calculateCartTotal(req.session.cart);
  
  res.render("guest/cart", { 
    cart: req.session.cart,
    total,
    cartCount: req.session.cart.reduce((sum, item) => sum + item.quantity, 0)
  });
});

// Update cart item quantity
router.post("/cart/update", (req, res) => {
  try {
    const { menuItemId, quantity } = req.body;
    initializeCart(req);

    const item = req.session.cart.find(item => item.menuItemId === menuItemId);
    if (item) {
      if (parseInt(quantity) <= 0) {
        // Remove item if quantity is 0 or less
        req.session.cart = req.session.cart.filter(item => item.menuItemId !== menuItemId);
      } else {
        item.quantity = parseInt(quantity);
      }
    }

    const total = calculateCartTotal(req.session.cart);
    const cartCount = req.session.cart.reduce((sum, item) => sum + item.quantity, 0);

    res.json({ 
      success: true, 
      total,
      cartCount
    });
  } catch (error) {
    console.error("Error updating cart:", error);
    res.status(500).json({ error: "Error updating cart" });
  }
});

// Remove item from cart
router.post("/cart/remove", (req, res) => {
  try {
    const { menuItemId } = req.body;
    initializeCart(req);

    req.session.cart = req.session.cart.filter(item => item.menuItemId !== menuItemId);
    
    const total = calculateCartTotal(req.session.cart);
    const cartCount = req.session.cart.reduce((sum, item) => sum + item.quantity, 0);

    res.json({ 
      success: true, 
      total,
      cartCount
    });
  } catch (error) {
    console.error("Error removing from cart:", error);
    res.status(500).json({ error: "Error removing from cart" });
  }
});

// ---------- CHECKOUT ROUTES ----------

// Show checkout page
router.get("/checkout", (req, res) => {
  initializeCart(req);
  
  if (req.session.cart.length === 0) {
    return res.redirect("/menu");
  }

  const total = calculateCartTotal(req.session.cart);
  
  res.render("guest/checkout", { 
    cart: req.session.cart,
    total,
    cartCount: req.session.cart.reduce((sum, item) => sum + item.quantity, 0)
  });
});

// Process checkout
router.post("/checkout", async (req, res) => {
  try {
    const { tableNumber } = req.body;
    initializeCart(req);

    if (req.session.cart.length === 0) {
      return res.json({ error: "Cart is empty" });
    }

    if (!tableNumber || tableNumber < 1) {
      return res.json({ error: "Please provide a valid table number" });
    }

    // Create order items array
    const orderItems = req.session.cart.map(item => ({
      menuItem: item.menuItemId,
      quantity: item.quantity
    }));

    const total = calculateCartTotal(req.session.cart);

    // Create new order
    const order = new Order({
      tableNumber: parseInt(tableNumber),
      items: orderItems,
      total,
      status: "pending"
    });

    await order.save();

    // Clear cart after successful order
    req.session.cart = [];

    res.json({ 
      success: true, 
      orderId: order._id,
      message: "Order placed successfully!" 
    });
  } catch (error) {
    console.error("Error processing checkout:", error);
    res.status(500).json({ error: "Error processing order" });
  }
});

// Order confirmation page
router.get("/order-confirmation/:orderId", async (req, res) => {
  try {
    const order = await Order.findById(req.params.orderId).populate("items.menuItem");
    if (!order) {
      return res.status(404).send("Order not found");
    }

    res.render("guest/order-confirmation", { order });
  } catch (error) {
    console.error("Error loading order confirmation:", error);
    res.status(500).send("Error loading order confirmation");
  }
});

module.exports = router;
